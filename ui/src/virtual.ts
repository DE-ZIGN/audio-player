// Drawing only what is in view (task 6-1): a library has tens of thousands
// of tracks; the list and the tiles keep a few dozen elements and fetch pages
// of rows as they come into view.

export interface Range {
  first: number; // inclusive
  last: number; // exclusive
}

export function visibleRange(o: {
  scrollTop: number;
  viewport: number;
  rowHeight: number;
  total: number;
  overscan: number;
}): Range {
  if (o.total <= 0 || o.rowHeight <= 0) return { first: 0, last: 0 };
  const first = Math.max(0, Math.floor(o.scrollTop / o.rowHeight) - o.overscan);
  const last = Math.min(o.total, Math.ceil((o.scrollTop + o.viewport) / o.rowHeight) + o.overscan);
  return { first: Math.min(first, last), last };
}

// As many columns as fit with tiles at least minTile wide; the tiles grow to fill the width.
export function gridLayout(o: { width: number; minTile: number; gap: number }): { columns: number; tile: number } {
  const columns = Math.max(1, Math.floor((o.width + o.gap) / (o.minTile + o.gap)));
  const tile = columns === 1 ? Math.min(o.width, Math.max(o.width, 0)) : (o.width - o.gap * (columns - 1)) / columns;
  return { columns, tile: Math.round(tile * 10) / 10 };
}

// Page numbers (of `pageSize` rows) a range needs.
export function pagesFor(r: Range, pageSize: number): number[] {
  if (r.last <= r.first) return [];
  const pages: number[] = [];
  for (let p = Math.floor(r.first / pageSize); p <= Math.floor((r.last - 1) / pageSize); p++) pages.push(p);
  return pages;
}
