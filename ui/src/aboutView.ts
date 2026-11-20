import {
  APP_LICENSE,
  APP_LICENSE_URL,
  APP_SOURCE_URL,
  bundledTree,
  loadAboutData,
  loadLicenseText,
  type AboutComponent,
  type AboutState,
  type LicenseText,
} from "./about";
import { onLangChange, t, type MessageKey } from "./i18n";

// The settings' 情報 / About tab (task 8-2): the app (its name, version,
// license and source), the trademark notice, and every distributed component
// with its copyright lines and its license texts in full, each text opened
// on demand. Everything is set as text, never as HTML: the texts are the
// components' own files.

function el<K extends keyof HTMLElementTagNameMap>(tag: K, className = "", text = ""): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text) e.textContent = text;
  return e;
}

// "ラベル  値" on one line.
function row(label: MessageKey, value: string): HTMLElement {
  const p = el("p", "about-row");
  p.append(el("span", "about-label", t(label)), el("span", "about-value", value));
  return p;
}

export class AboutView {
  private readonly app = document.getElementById("about-app") as HTMLElement | null;
  private readonly list = document.getElementById("about-components") as HTMLElement | null;
  private state: AboutState | null = null;
  // The texts read so far, kept across a change of language (the view is drawn again).
  private readonly texts = new Map<string, Promise<string>>();

  constructor(kind: "app" | "mock") {
    onLangChange(() => this.render());
    this.render();
    void loadAboutData(kind).then((s) => {
      this.state = s;
      this.render();
    });
  }

  private render() {
    this.renderApp();
    this.renderComponents();
  }

  private renderApp() {
    if (!this.app) return;
    const data = this.state?.state === "ok" ? this.state.data : null;
    const name = el("p", "about-name");
    // Without the build's data (the browser's mock) the page's own title is the name.
    name.append(el("b", "", data?.app.name ?? document.title));
    if (data) name.append(" ", el("span", "", t("about.app.version", { version: data.app.version })));
    const parts: Node[] = [
      name,
      el("p", "note", t("about.app.free")),
      row("about.label.license", `${APP_LICENSE} — ${APP_LICENSE_URL}`),
      row("about.label.source", APP_SOURCE_URL),
      ...(data?.app.texts ?? []).map((x) => this.textDetails(x)),
      el("h3", "", t("about.trademarks")),
      el("p", "note", t("about.trademark.asio")),
    ];
    this.app.replaceChildren(...parts);
  }

  private renderComponents() {
    if (!this.list) return;
    const s = this.state;
    if (s === null) return this.list.replaceChildren();
    if (s.state === "none") return this.list.replaceChildren(el("p", "note", t("about.noData")));
    if (s.state === "error") return this.list.replaceChildren(el("p", "notice", `⚠ ${t("about.loadError", { detail: s.detail })}`));
    const nodes: Node[] = [];
    for (const { component, bundled } of bundledTree(s.data.components)) {
      nodes.push(this.component(component, null));
      for (const b of bundled) nodes.push(this.component(b, component));
    }
    this.list.replaceChildren(...nodes);
  }

  private component(c: AboutComponent, parent: AboutComponent | null): HTMLElement {
    const box = el("div", parent ? "about-component bundled" : "about-component");
    box.dataset.component = c.id;
    const h = el("h3", "", c.name);
    if (parent) h.append(" ", el("small", "", t("about.bundledIn", { name: parent.name })));
    box.append(
      h,
      row("about.label.version", c.version),
      row("about.label.license", c.license),
      row("about.label.url", c.url),
      row("about.label.copyright", c.copyright.join("\n")),
    );
    if (c.note) box.append(el("p", "note", t(c.note)));
    for (const x of c.texts) box.append(this.textDetails(x));
    return box;
  }

  // One license text, read when first opened (a 詳細-like button, the text in a pre).
  private textDetails(x: LicenseText): HTMLElement {
    const d = el("details", "about-license");
    const pre = el("pre", "about-text");
    d.append(el("summary", "", t("about.licenseText", { name: x.name })), pre);
    const show = () => {
      let text = this.texts.get(x.file);
      if (!text) {
        text = loadLicenseText(x.file);
        this.texts.set(x.file, text);
        text.catch(() => this.texts.delete(x.file)); // tried again when opened again
      }
      pre.textContent = t("about.textLoading");
      text.then(
        (body) => (pre.textContent = body),
        (e) => {
          const p = el("p", "notice", `⚠ ${t("about.textError", { detail: String(e) })}`);
          pre.replaceWith(p);
        },
      );
    };
    d.addEventListener("toggle", () => {
      if (d.open && pre.isConnected && pre.textContent === "") show();
    });
    return d;
  }
}
