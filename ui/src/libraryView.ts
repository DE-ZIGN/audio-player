// The library part of the library view (task 6-1): the track list, the album
// tiles and an album's tracks, sorted and searched; the watched folders; the
// themes. Lists of any length keep only what is in view in the page and
// fetch rows a page at a time (virtual.ts).

import type { Backend } from "./backend";
import type { AlbumRow, AlbumSort, Folder, LibraryCounts, TrackRow, TrackSort } from "./library";
import { dbtpText, formatText, lengthText, rateText, truePeakCell } from "./libraryFormat";
import { gridLayout, pagesFor, visibleRange } from "./virtual";

export type View = "tracks" | "albums" | "album" | "settings";
export type Theme = "amber" | "bluegray";

export interface PlayRequest {
  ids: number[];
  start: number;
}

export type Enqueue = (ids: number[], where: "next" | "end") => void;

const PAGE = 200;
const ROW_H = 30;
const TILE_MIN = 168;
const TILE_GAP = 18;
const TILE_TEXT = 48;
const PLAY_LIMIT = 1000; // tracks put in the queue from a long list

const TRACK_SORTS: [TrackSort, string][] = [
  ["artist", "アーティスト順"],
  ["album", "アルバム順"],
  ["title", "曲名順"],
  ["added", "追加した順（新しい順）"],
  ["length", "長さ順"],
  ["format", "形式順"],
  ["rate", "レート順"],
  ["truePeak", "True Peak 順（高い順）"],
  ["plays", "再生回数順（多い順）"],
];
const ALBUM_SORTS: [AlbumSort, string][] = [
  ["artist", "アーティスト順"],
  ["album", "アルバム名順"],
  ["year", "年順（新しい順）"],
  ["added", "追加した順（新しい順）"],
];
// Sorts that read best from the top down.
const DESCENDING_FIRST = new Set<string>(["added", "truePeak", "plays", "year"]);

interface Column {
  key: TrackSort | null;
  label: string;
  num?: boolean;
  cell: (r: TrackRow, el: HTMLSpanElement) => void;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

export class LibraryView {
  private view: View = "tracks";
  private search = "";
  private trackSort: TrackSort = "artist";
  private trackDesc = false;
  private albumSort: AlbumSort = "artist";
  private albumDesc = false;
  private album: AlbumRow | null = null;
  private generation = 0; // changes with every query: late answers are dropped
  private total = 0;
  private pages = new Map<number, TrackRow[] | AlbumRow[] | "loading">();
  private selected = -1;
  private playingId: number | null = null;
  private frame = 0;
  private lastCounts = "";

  private readonly els = {
    title: $("content-title"),
    search: $<HTMLInputElement>("search"),
    sort: $<HTMLSelectElement>("sort"),
    viewList: $<HTMLButtonElement>("view-list"),
    viewGrid: $<HTMLButtonElement>("view-grid"),
    table: $("table"),
    head: $("table-head"),
    body: $("table-body"),
    bodySpacer: $("table-spacer"),
    grid: $("grid"),
    gridSpacer: $("grid-spacer"),
    empty: $("empty"),
    settings: $("settings"),
    albumHead: $("album-head"),
    albumBack: $<HTMLButtonElement>("album-back"),
    albumCover: $<HTMLImageElement>("album-cover"),
    albumTitle: $("album-title"),
    albumArtist: $("album-artist"),
    albumMeta: $("album-meta"),
    folders: $("folder-list"),
    addFolder: $<HTMLButtonElement>("add-folder"),
    error: $("error"),
  };

  private readonly menu = $("menu");

  constructor(
    private readonly backend: Backend,
    private readonly onPlay: (req: PlayRequest) => void,
    private readonly onEnqueue: Enqueue,
  ) {
    // The context menu closes on any click elsewhere, Escape, or scrolling.
    document.addEventListener("click", () => this.hideMenu());
    document.addEventListener("keydown", (e) => e.key === "Escape" && this.hideMenu());
    document.addEventListener("scroll", () => this.hideMenu(), true);
    this.els.search.addEventListener("input", () => {
      clearTimeout(this.searchTimer);
      this.searchTimer = setTimeout(() => {
        this.search = this.els.search.value;
        this.reload(true);
      }, 200);
    });
    this.els.sort.addEventListener("change", () => {
      const v = this.els.sort.value;
      if (this.view === "albums") {
        this.albumSort = v as AlbumSort;
        this.albumDesc = DESCENDING_FIRST.has(v);
      } else {
        this.trackSort = v as TrackSort;
        this.trackDesc = DESCENDING_FIRST.has(v);
      }
      this.reload(true);
    });
    this.els.viewList.addEventListener("click", () => this.show("tracks"));
    this.els.viewGrid.addEventListener("click", () => this.show("albums"));
    this.els.albumBack.addEventListener("click", () => this.show("albums"));
    this.els.body.addEventListener("scroll", () => this.schedule());
    this.els.grid.addEventListener("scroll", () => this.schedule());
    new ResizeObserver(() => this.schedule()).observe(this.els.body);
    new ResizeObserver(() => this.schedule()).observe(this.els.grid);
    this.els.addFolder.addEventListener("click", async () => {
      const r = await this.backend.library.addFolder();
      if (!r.ok && !r.cancelled && r.message) this.showError(r.message);
      await this.refreshFolders();
    });
    this.els.body.addEventListener("keydown", (e) => this.onKey(e));
    this.els.body.tabIndex = 0;
    for (const radio of document.querySelectorAll<HTMLInputElement>('input[name="theme"]'))
      radio.addEventListener("change", () => radio.checked && setTheme(radio.value as Theme));
    document.querySelectorAll<HTMLButtonElement>(".nav").forEach((b) =>
      b.addEventListener("click", () => this.show(b.dataset.view as View)),
    );
    this.backend.library.onChanged((c) => this.onCounts(c));
  }

  private searchTimer: ReturnType<typeof setTimeout> | undefined;
  private reloadTimer: ReturnType<typeof setTimeout> | undefined;

  async start() {
    this.show("tracks");
    await this.refreshFolders();
  }

  // --- what is shown ------------------------------------------------------

  show(view: View, album: AlbumRow | null = null) {
    this.view = view;
    this.album = album;
    document.querySelectorAll<HTMLButtonElement>(".nav").forEach((b) =>
      b.classList.toggle("active", b.dataset.view === (view === "album" ? "albums" : view)),
    );
    const library = view !== "settings";
    this.els.search.hidden = !library;
    this.els.sort.hidden = !library || view === "album";
    this.els.viewList.parentElement!.hidden = !library || view === "album";
    this.els.viewList.classList.toggle("active", view === "tracks");
    this.els.viewGrid.classList.toggle("active", view === "albums");
    this.els.table.hidden = !(view === "tracks" || view === "album");
    this.els.grid.hidden = view !== "albums";
    this.els.settings.hidden = view !== "settings";
    this.els.albumHead.hidden = view !== "album";
    this.els.title.textContent =
      view === "settings" ? "設定" : view === "albums" ? "アルバム" : view === "album" ? "アルバム" : "ライブラリ";
    if (view === "settings") {
      this.els.empty.hidden = true;
      for (const r of document.querySelectorAll<HTMLInputElement>('input[name="theme"]'))
        r.checked = r.value === currentTheme();
      return;
    }
    this.fillSort();
    if (view === "album" && album) this.fillAlbumHead(album);
    this.renderHead();
    this.reload(true);
  }

  private fillSort() {
    const options = this.view === "albums" ? ALBUM_SORTS : TRACK_SORTS;
    const current = this.view === "albums" ? this.albumSort : this.trackSort;
    this.els.sort.replaceChildren(
      ...options.map(([value, label]) => {
        const o = document.createElement("option");
        o.value = value;
        o.textContent = label;
        o.selected = value === current;
        return o;
      }),
    );
  }

  private fillAlbumHead(a: AlbumRow) {
    this.els.albumCover.src = this.backend.library.coverUrl(a.coverTrack);
    this.els.albumCover.onerror = () => (this.els.albumCover.removeAttribute("src"), undefined);
    this.els.albumTitle.textContent = a.album;
    this.els.albumArtist.textContent = a.artist;
    const meta = [
      a.year,
      `${a.tracks} 曲`,
      a.seconds !== null ? lengthText(a.seconds) : null,
      a.container || null,
      a.sampleRate ? rateText({ sampleRate: a.sampleRate, dsdRate: null }) : null,
      a.truePeakDb !== null ? `アルバムの True Peak ${dbtpText(a.truePeakDb)}${a.truePeakDb > 0 ? " ⚠" : ""}` : null,
    ].filter((x): x is string => !!x);
    this.els.albumMeta.replaceChildren(
      ...meta.map((m) => {
        const s = document.createElement("span");
        s.className = "tag";
        s.textContent = m;
        return s;
      }),
    );
  }

  // Starts the current list again (a new query), keeping the scroll position when asked.
  private reload(resetScroll: boolean) {
    this.generation++;
    this.pages.clear();
    this.total = 0;
    this.selected = resetScroll ? -1 : this.selected;
    if (resetScroll) {
      this.els.body.scrollTop = 0;
      this.els.grid.scrollTop = 0;
    }
    void this.fetchPage(0).then(() => this.schedule());
  }

  // --- data ---------------------------------------------------------------

  private async fetchPage(page: number) {
    if (this.pages.has(page)) return;
    const gen = this.generation;
    this.pages.set(page, "loading");
    try {
      const lib = this.backend.library;
      const result =
        this.view === "albums"
          ? await lib.albums({
              search: this.search,
              sort: this.albumSort,
              descending: this.albumDesc,
              offset: page * PAGE,
              limit: PAGE,
            })
          : await lib.tracks({
              search: this.view === "album" ? "" : this.search,
              sort: this.trackSort,
              descending: this.trackDesc,
              albumKey: this.view === "album" ? this.album?.key : undefined,
              offset: page * PAGE,
              limit: PAGE,
            });
      if (gen !== this.generation) return;
      this.pages.set(page, result.rows as TrackRow[] | AlbumRow[]);
      this.total = result.total;
    } catch (e) {
      if (gen === this.generation) this.pages.delete(page);
      this.showError(`ライブラリを読めませんでした: ${String(e)}`);
    }
    this.schedule();
  }

  private rowAt<T>(i: number): T | undefined {
    const p = this.pages.get(Math.floor(i / PAGE));
    return Array.isArray(p) ? (p[i % PAGE] as T | undefined) : undefined;
  }

  // --- drawing ------------------------------------------------------------

  private schedule() {
    if (this.frame) return;
    this.frame = requestAnimationFrame(() => {
      this.frame = 0;
      if (this.view === "albums") this.drawGrid();
      else if (this.view === "tracks" || this.view === "album") this.drawRows();
    });
  }

  private columns(): Column[] {
    const text = (f: (r: TrackRow) => string) => (r: TrackRow, el: HTMLSpanElement) => {
      el.textContent = f(r);
      el.title = el.textContent;
    };
    return [
      {
        key: null,
        label: this.view === "album" ? "#" : "",
        cell: (r, el) => {
          el.className = r.id === this.playingId ? "mark" : "dim";
          el.textContent = r.id === this.playingId ? "▶" : this.view === "album" ? String(r.trackNo ?? "") : "";
        },
      },
      { key: "title", label: "曲名", cell: text((r) => r.title) },
      { key: "artist", label: "アーティスト", cell: text((r) => r.artist || "—") },
      { key: "album", label: "アルバム", cell: text((r) => r.album || "—") },
      { key: "length", label: "長さ", num: true, cell: text((r) => lengthText(r.seconds)) },
      { key: "format", label: "形式", cell: text((r) => formatText(r)) },
      { key: "rate", label: "レート", cell: text((r) => rateText(r)) },
      {
        key: "truePeak",
        label: "True Peak",
        num: true,
        cell: (r, el) => {
          const c = truePeakCell(r);
          el.textContent = c.text;
          el.title = c.title;
          el.className = `num peak${c.over ? " over" : ""}${r.analysis !== "ok" ? " dim" : ""}`;
        },
      },
      { key: "plays", label: "再生", num: true, cell: text((r) => String(r.plays)) },
    ];
  }

  private renderHead() {
    const cols = this.columns();
    this.els.head.replaceChildren(
      ...cols.map((c) => {
        const el = document.createElement(c.key && this.view !== "album" ? "button" : "span");
        el.textContent = c.label;
        if (c.num) el.classList.add("num");
        if (c.key && this.view !== "album") {
          const key = c.key;
          el.classList.toggle("sorted", key === this.trackSort);
          if (key === this.trackSort) el.textContent += this.trackDesc ? " ▼" : " ▲";
          el.addEventListener("click", () => {
            if (this.trackSort === key) this.trackDesc = !this.trackDesc;
            else {
              this.trackSort = key;
              this.trackDesc = DESCENDING_FIRST.has(key);
            }
            this.fillSort();
            this.renderHead();
            this.reload(true);
          });
        }
        return el;
      }),
    );
  }

  private drawRows() {
    const body = this.els.body;
    this.els.bodySpacer.style.height = `${this.total * ROW_H}px`;
    const range = visibleRange({
      scrollTop: body.scrollTop,
      viewport: body.clientHeight,
      rowHeight: ROW_H,
      total: this.total,
      overscan: 10,
    });
    for (const p of pagesFor(range, PAGE)) void this.fetchPage(p);
    const cols = this.columns();
    const rows: HTMLElement[] = [];
    for (let i = range.first; i < range.last; i++) {
      const r = this.rowAt<TrackRow>(i);
      const row = document.createElement("div");
      row.className = "row";
      row.style.top = `${i * ROW_H}px`;
      row.dataset.index = String(i);
      if (!r) {
        row.innerHTML = "<span></span><span class='dim'>…</span>";
      } else {
        for (const c of cols) {
          const span = document.createElement("span");
          if (c.num) span.className = "num";
          c.cell(r, span);
          row.append(span);
        }
        if (!r.present) {
          row.classList.add("missing");
          row.title = "ファイルが見つかりません（ドライブがつながっていない、移動した、など）";
        }
        if (r.id === this.playingId) row.classList.add("playing");
        if (i === this.selected) row.classList.add("selected");
        row.addEventListener("click", () => this.select(i));
        row.addEventListener("dblclick", () => void this.playFrom(i));
        row.addEventListener("contextmenu", (e) => {
          e.preventDefault();
          this.select(i);
          this.showMenu(e, [
            ["ここから再生", () => void this.playFrom(i)],
            ["次に再生", () => this.onEnqueue([r.id], "next")],
            ["キューの最後に追加", () => this.onEnqueue([r.id], "end")],
          ]);
        });
      }
      rows.push(row);
    }
    body.replaceChildren(this.els.bodySpacer, ...rows);
    this.showEmpty(this.pages.size > 0 && ![...this.pages.values()].includes("loading") && this.total === 0);
  }

  private drawGrid() {
    const grid = this.els.grid;
    const width = grid.clientWidth - 4;
    const { columns, tile } = gridLayout({ width, minTile: TILE_MIN, gap: TILE_GAP });
    const rowH = tile + TILE_TEXT + TILE_GAP;
    const rowsTotal = Math.ceil(this.total / columns);
    this.els.gridSpacer.style.height = `${rowsTotal * rowH}px`;
    const range = visibleRange({ scrollTop: grid.scrollTop, viewport: grid.clientHeight, rowHeight: rowH, total: rowsTotal, overscan: 2 });
    const first = range.first * columns;
    const last = Math.min(this.total, range.last * columns);
    for (const p of pagesFor({ first, last }, PAGE)) void this.fetchPage(p);
    const tiles: HTMLElement[] = [];
    for (let i = first; i < last; i++) {
      const a = this.rowAt<AlbumRow>(i);
      const el = document.createElement("div");
      el.className = "tile";
      el.style.width = `${tile}px`;
      el.style.left = `${(i % columns) * (tile + TILE_GAP)}px`;
      el.style.top = `${Math.floor(i / columns) * rowH}px`;
      if (a) {
        const img = document.createElement("img");
        img.loading = "lazy";
        img.alt = "";
        img.src = this.backend.library.coverUrl(a.coverTrack);
        img.addEventListener("error", () => {
          const blank = document.createElement("div");
          blank.className = "cover-blank";
          blank.textContent = (a.album || "?").slice(0, 1);
          img.replaceWith(blank);
        });
        const title = document.createElement("div");
        title.className = "tile-title";
        title.textContent = a.album;
        title.title = a.album;
        const sub = document.createElement("div");
        sub.className = "tile-sub";
        sub.textContent = [a.artist, a.year].filter(Boolean).join(" ・ ");
        el.append(img, title, sub);
        el.title = `${a.album} — ${a.artist}（${a.tracks} 曲）`;
        el.addEventListener("click", () => this.show("album", a));
        el.addEventListener("contextmenu", (e) => {
          e.preventDefault();
          this.showMenu(e, [
            ["アルバムを再生", async () => this.onPlay({ ids: await this.albumIds(a), start: 0 })],
            ["アルバムを次に再生", async () => this.onEnqueue(await this.albumIds(a), "next")],
            ["アルバムをキューの最後に追加", async () => this.onEnqueue(await this.albumIds(a), "end")],
          ]);
        });
      }
      tiles.push(el);
    }
    grid.replaceChildren(this.els.gridSpacer, ...tiles);
    this.showEmpty(this.pages.size > 0 && ![...this.pages.values()].includes("loading") && this.total === 0);
  }

  private showEmpty(empty: boolean) {
    this.els.empty.hidden = !empty;
    if (empty)
      this.els.empty.textContent = this.search
        ? "見つかりませんでした。かな・全角半角・大文字小文字は区別しません。漢字の読みでは探せません。"
        : this.view === "albums"
          ? "アルバムがありません（アルバム名のタグがある曲が、ここに並びます）"
          : "曲がありません。左の「監視フォルダー」の「＋」で、音楽のフォルダーを加えてください。";
  }

  // --- choosing and playing -------------------------------------------------

  private select(i: number) {
    this.selected = i;
    this.schedule();
  }

  private onKey(e: KeyboardEvent) {
    if (this.view !== "tracks" && this.view !== "album") return;
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      const i = Math.max(0, Math.min(this.total - 1, this.selected + (e.key === "ArrowDown" ? 1 : -1)));
      this.select(i);
      const top = i * ROW_H;
      const body = this.els.body;
      if (top < body.scrollTop) body.scrollTop = top;
      else if (top + ROW_H > body.scrollTop + body.clientHeight) body.scrollTop = top + ROW_H - body.clientHeight;
    } else if (e.key === "Enter" && this.selected >= 0) {
      void this.playFrom(this.selected);
    }
  }

  private async albumIds(a: AlbumRow): Promise<number[]> {
    const page = await this.backend.library.tracks({ albumKey: a.key, offset: 0, limit: 2000 });
    return page.rows.filter((r) => r.present).map((r) => r.id);
  }

  private showMenu(e: MouseEvent, items: [string, () => void][]) {
    this.menu.replaceChildren(
      ...items.map(([label, action]) => {
        const b = document.createElement("button");
        b.type = "button";
        b.textContent = label;
        b.addEventListener("click", (ev) => {
          ev.stopPropagation();
          this.hideMenu();
          action();
        });
        return b;
      }),
    );
    this.menu.hidden = false;
    const w = this.menu.offsetWidth;
    const h = this.menu.offsetHeight;
    this.menu.style.left = `${Math.min(e.clientX, window.innerWidth - w - 4)}px`;
    this.menu.style.top = `${Math.min(e.clientY, window.innerHeight - h - 4)}px`;
  }

  private hideMenu() {
    this.menu.hidden = true;
  }

  // Plays from row i: an album whole (from that track), or the list from that row on.
  private async playFrom(i: number) {
    const lib = this.backend.library;
    const album = this.view === "album";
    const page = await lib.tracks({
      search: album ? "" : this.search,
      sort: this.trackSort,
      descending: this.trackDesc,
      albumKey: album ? this.album?.key : undefined,
      offset: album ? 0 : i,
      limit: album ? 2000 : PLAY_LIMIT,
    });
    const rows = page.rows.filter((r) => r.present);
    if (rows.length === 0) return this.showError("再生できる曲がありません（ファイルが見つかりません）");
    const clicked = album ? page.rows[i] : page.rows[0];
    const start = Math.max(0, rows.findIndex((r) => r.id === clicked?.id));
    this.onPlay({ ids: rows.map((r) => r.id), start });
  }

  setPlaying(id: number | null) {
    if (id === this.playingId) return;
    this.playingId = id;
    this.schedule();
  }

  // --- folders, counts, errors --------------------------------------------

  async refreshFolders() {
    const folders = await this.backend.library.folders();
    this.els.folders.replaceChildren(...folders.map((f) => this.folderItem(f)));
  }

  private folderItem(f: Folder): HTMLElement {
    const li = document.createElement("li");
    const icon = document.createElement("span");
    icon.textContent = "▢";
    icon.className = "dim";
    const path = document.createElement("span");
    path.className = "folder-path";
    path.textContent = f.path;
    path.title = `${f.path}\n${f.tracks} 曲${f.missing ? `（見つからない ${f.missing} 曲）` : ""}`;
    const count = document.createElement("span");
    count.className = "folder-count";
    count.textContent = String(f.tracks);
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "icon small";
    remove.textContent = "×";
    remove.title = "このフォルダーをライブラリから外す";
    remove.addEventListener("click", async () => {
      const ok = window.confirm(
        `「${f.path}」をライブラリから外します。\n中の曲の再生回数・評価・修正も消えます（音楽ファイルは消しません）。よろしいですか？`,
      );
      if (!ok) return;
      await this.backend.library.removeFolder(f.id);
      await this.refreshFolders();
      this.reload(false);
    });
    li.append(icon, path, count, remove);
    return li;
  }

  private onCounts(c: LibraryCounts) {
    // While a scan or the analysis runs, refresh what is shown now and then.
    const key = `${c.tracks}/${c.albums}/${c.toMeasure}/${c.missing}/${c.folders}`;
    if (key === this.lastCounts) return;
    const foldersChanged = this.lastCounts.split("/")[4] !== String(c.folders);
    this.lastCounts = key;
    if (foldersChanged) void this.refreshFolders();
    clearTimeout(this.reloadTimer);
    this.reloadTimer = setTimeout(() => {
      if (this.view !== "settings") this.reload(false);
      void this.refreshFolders();
    }, 1500);
  }

  showError(message: string) {
    this.els.error.hidden = false;
    this.els.error.textContent = message;
  }
}

// --- themes (CLAUDE.md chapter 8: two built-in themes, colour only) ----------

export function currentTheme(): Theme {
  return document.documentElement.dataset.theme === "bluegray" ? "bluegray" : "amber";
}

export function setTheme(theme: Theme) {
  document.documentElement.dataset.theme = theme;
  try {
    localStorage.setItem("theme", theme);
  } catch {
    // no storage: the theme lasts until the window closes
  }
}

export function loadTheme() {
  let saved: string | null = null;
  try {
    saved = localStorage.getItem("theme");
  } catch {
    saved = null;
  }
  document.documentElement.dataset.theme = saved === "bluegray" ? "bluegray" : "amber";
}
