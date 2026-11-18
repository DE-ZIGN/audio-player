import { describe, expect, it } from "vitest";
import { parseArticle, parseInlines } from "./markdown";
import index from "../../public/articles/index.json?raw";
import about from "../../public/articles/about-articles.md?raw";
import { isArticleId } from "./links";

// Task 7-1: the small part of Markdown the reader shows for now (headings,
// paragraphs, lists, links). The rest is task 7-2.

describe("the reader's Markdown (the part needed for task 7-1)", () => {
  it("headings with a fixed id, paragraphs and lists", () => {
    const blocks = parseArticle(
      ["# 題 {#top}", "", "一つ目の段落。", "続き。", "", "## 節", "- あ", "- [い](abx.md)", "", "終わり。"].join("\n"),
    );
    expect(blocks).toEqual([
      { kind: "heading", level: 1, id: "top", inlines: [{ kind: "text", text: "題" }] },
      { kind: "paragraph", inlines: [{ kind: "text", text: "一つ目の段落。続き。" }] },
      { kind: "heading", level: 2, id: "", inlines: [{ kind: "text", text: "節" }] },
      {
        kind: "list",
        items: [[{ kind: "text", text: "あ" }], [{ kind: "link", text: "い", href: "abx.md" }]],
      },
      { kind: "paragraph", inlines: [{ kind: "text", text: "終わり。" }] },
    ]);
  });
  it("lines of a paragraph join with a space only between Latin words", () => {
    expect(parseArticle("one\ntwo")).toEqual([{ kind: "paragraph", inlines: [{ kind: "text", text: "one two" }] }]);
    expect(parseArticle("日本語\nの文")).toEqual([{ kind: "paragraph", inlines: [{ kind: "text", text: "日本語の文" }] }]);
  });
  it("links, and text that only looks like one", () => {
    expect(parseInlines("前 [ABX](app:abx) 後")).toEqual([
      { kind: "text", text: "前 " },
      { kind: "link", text: "ABX", href: "app:abx" },
      { kind: "text", text: " 後" },
    ]);
    expect(parseInlines("[a](b c)")).toEqual([{ kind: "text", text: "[a](b c)" }]);
    expect(parseInlines("[]()")).toEqual([{ kind: "text", text: "[]()" }]);
  });
  it("HTML in an article stays text", () => {
    const blocks = parseArticle('<script>alert(1)</script>\n\n<img src=x onerror="alert(1)">');
    expect(blocks.map((b) => b.kind)).toEqual(["paragraph", "paragraph"]);
    expect(blocks[0]).toEqual({ kind: "paragraph", inlines: [{ kind: "text", text: "<script>alert(1)</script>" }] });
  });
  it("a heading id that is not letters, digits and hyphens is dropped", () => {
    expect(parseArticle('## 節 {#a"b}')[0]).toEqual({
      kind: "heading",
      level: 2,
      id: "",
      inlines: [{ kind: "text", text: '節 {#a"b}' }],
    });
  });
});

describe("the bundled articles", () => {
  const entries = JSON.parse(index) as { id: string; title: string }[];
  it("the list has valid ids and titles", () => {
    expect(entries.length).toBeGreaterThan(0);
    for (const e of entries) {
      expect(isArticleId(e.id), e.id).toBe(true);
      expect(e.title.length).toBeGreaterThan(0);
    }
    expect(entries.map((e) => e.id)).toContain("about-articles");
  });
  it("the first article links to the three screens an article may open", () => {
    const links = parseArticle(about)
      .flatMap((b) => (b.kind === "list" ? b.items.flat() : b.inlines))
      .flatMap((i) => (i.kind === "link" ? [i.href] : []));
    expect(links).toEqual(expect.arrayContaining(["app:abx", "app:output-check", "app:settings"]));
  });
});
