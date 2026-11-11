import { describe, expect, it } from "vitest";
import { gridLayout, pagesFor, visibleRange } from "./virtual";

describe("Only what is in view is drawn (task 6-1)", () => {
  it("gives the rows in view with some to spare", () => {
    expect(visibleRange({ scrollTop: 0, viewport: 300, rowHeight: 30, total: 1000, overscan: 5 })).toEqual({
      first: 0,
      last: 15,
    });
    expect(visibleRange({ scrollTop: 3000, viewport: 300, rowHeight: 30, total: 1000, overscan: 5 })).toEqual({
      first: 95,
      last: 115,
    });
    expect(visibleRange({ scrollTop: 29_000, viewport: 300, rowHeight: 30, total: 1000, overscan: 5 })).toEqual({
      first: 961,
      last: 982,
    });
    expect(visibleRange({ scrollTop: 29_900, viewport: 300, rowHeight: 30, total: 1000, overscan: 5 })).toEqual({
      first: 991,
      last: 1000, // never past the end
    });
    expect(visibleRange({ scrollTop: 0, viewport: 300, rowHeight: 30, total: 0, overscan: 5 })).toEqual({
      first: 0,
      last: 0,
    });
  });
  it("lays tiles out in as many columns as fit", () => {
    expect(gridLayout({ width: 1000, minTile: 160, gap: 16 })).toEqual({ columns: 5, tile: 187.2 });
    expect(gridLayout({ width: 100, minTile: 160, gap: 16 })).toEqual({ columns: 1, tile: 100 });
  });
  it("names the pages a range of rows needs", () => {
    expect(pagesFor({ first: 0, last: 15 }, 200)).toEqual([0]);
    expect(pagesFor({ first: 190, last: 210 }, 200)).toEqual([0, 1]);
    expect(pagesFor({ first: 0, last: 0 }, 200)).toEqual([]);
  });
});
