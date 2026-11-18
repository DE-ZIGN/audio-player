import { describe, expect, it } from "vitest";
import { isArticleId, isSection, linkKind, linkTarget } from "./links";
import html from "../../reader.html?raw";

// Task 7-1: the reader's page follows a link to another article itself;
// everything else it hands to the app, which decides (ArticlePolicy.h).

describe("where a link in an article goes", () => {
  it("another article, by its id (with or without .md), and a section", () => {
    expect(linkTarget("bit-perfect")).toEqual({ kind: "article", id: "bit-perfect", section: "" });
    expect(linkTarget("abx.md")).toEqual({ kind: "article", id: "abx", section: "" });
    expect(linkTarget("abx.md#level-matching")).toEqual({ kind: "article", id: "abx", section: "level-matching" });
    expect(linkTarget("./abx.md")).toEqual({ kind: "article", id: "abx", section: "" });
  });
  it("a section of this article", () => {
    expect(linkTarget("#links")).toEqual({ kind: "article", id: "", section: "links" });
  });
  it("everything else is the app's to decide", () => {
    for (const href of [
      "app:abx",
      "app:settings/eq",
      "app:delete-library",
      "https://example.com/a",
      "javascript:alert(1)",
      "file:///C:/x",
      "../index.html",
      "articles/abx.md",
      "Abx",
      "abx.md?x=1",
      "#a b",
      "",
    ]) {
      expect(linkTarget(href), href).toEqual({ kind: "native", url: href });
    }
  });
  it("ids and sections", () => {
    expect(isArticleId("bit-perfect")).toBe(true);
    expect(isArticleId("a1")).toBe(true);
    for (const bad of ["", "-a", "A", "a/b", "a.md", "..", "a".repeat(65)]) expect(isArticleId(bad), bad).toBe(false);
    expect(isSection("level-matching")).toBe(true);
    expect(isSection("Sec2")).toBe(true);
    for (const bad of ["", "a b", "a#b", "a".repeat(65)]) expect(isSection(bad), bad).toBe(false);
  });
  it("how a link is marked (looks only; the app decides what happens)", () => {
    expect(linkKind("abx.md")).toBe("article");
    expect(linkKind("#links")).toBe("article");
    expect(linkKind("app:abx")).toBe("command");
    expect(linkKind("https://example.com/")).toBe("web");
    expect(linkKind("http://example.com/")).toBe("web");
    expect(linkKind("javascript:alert(1)")).toBe("other");
  });
});

describe("the reader's page loads nothing from outside", () => {
  const csp = /http-equiv="Content-Security-Policy"\s+content="([^"]*)"/.exec(html)?.[1] ?? "";
  it("has a policy that starts from nothing and allows only its own files", () => {
    expect(csp).toContain("default-src 'none'");
    for (const part of ["script-src", "style-src", "img-src", "connect-src", "font-src"]) {
      expect(csp, part).toMatch(new RegExp(`${part} 'self'(;|$)`));
    }
    expect(csp).toContain("base-uri 'none'");
    expect(csp).toContain("form-action 'none'");
  });
  it("no inline script or style, no eval, no other site", () => {
    expect(csp).not.toMatch(/unsafe-inline|unsafe-eval|https?:|wss?:|data:|blob:|\*/);
    expect(html).not.toMatch(/<script(?![^>]*\bsrc=)[^>]*>/); // every script is a file
    expect(html).not.toMatch(/\sstyle="/);
    expect(html).not.toMatch(/\son[a-z]+="/);
  });
});
