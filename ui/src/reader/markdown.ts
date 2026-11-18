// Task 7-1: the small part of Markdown the reader shows for now: headings
// ("#" to "###", with an optional fixed id "{#id}"), paragraphs, "- " lists
// and [text](address) links. Everything else stays plain text; the page
// builds elements from this and never parses an article as HTML.
// Emphasis, tables, images and the rest are task 7-2.

export type Inline = { kind: "text"; text: string } | { kind: "link"; text: string; href: string };

export type Block =
  | { kind: "heading"; level: 1 | 2 | 3; id: string; inlines: Inline[] }
  | { kind: "paragraph"; inlines: Inline[] }
  | { kind: "list"; items: Inline[][] };

const LINK = /\[([^\]\n]+)\]\(([^()\s]+)\)/g;
const HEADING = /^(#{1,3})\s+(.*)$/;
const HEADING_ID = /^(.*?)\s*\{#([A-Za-z0-9-]{1,64})\}$/;
const ITEM = /^[-*]\s+(.*)$/;

export function parseInlines(text: string): Inline[] {
  const out: Inline[] = [];
  let last = 0;
  for (const m of text.matchAll(LINK)) {
    if (m.index > last) out.push({ kind: "text", text: text.slice(last, m.index) });
    out.push({ kind: "link", text: m[1], href: m[2] });
    last = m.index + m[0].length;
  }
  if (last < text.length) out.push({ kind: "text", text: text.slice(last) });
  return out;
}

// A line break inside a paragraph: a space between Latin words, nothing
// between Japanese characters.
function joinLines(lines: string[]): string {
  let out = "";
  for (const line of lines) {
    const latin = /[\x21-\x7e]$/.test(out) && /^[\x21-\x7e]/.test(line);
    out += (latin ? " " : "") + line;
  }
  return out;
}

export function parseArticle(markdown: string): Block[] {
  const blocks: Block[] = [];
  let paragraph: string[] = [];
  let list: Inline[][] | null = null;
  const flush = () => {
    if (paragraph.length) blocks.push({ kind: "paragraph", inlines: parseInlines(joinLines(paragraph)) });
    if (list) blocks.push({ kind: "list", items: list });
    paragraph = [];
    list = null;
  };

  for (const raw of markdown.replace(/\r\n?/g, "\n").split("\n")) {
    const line = raw.trim();
    if (line === "") {
      flush();
      continue;
    }
    const heading = HEADING.exec(line);
    if (heading) {
      flush();
      const withId = HEADING_ID.exec(heading[2]);
      blocks.push({
        kind: "heading",
        level: heading[1].length as 1 | 2 | 3,
        id: withId ? withId[2] : "",
        inlines: parseInlines(withId ? withId[1] : heading[2]),
      });
      continue;
    }
    const item = ITEM.exec(line);
    if (item) {
      if (paragraph.length) flush();
      (list ??= []).push(parseInlines(item[1]));
      continue;
    }
    if (list) flush();
    paragraph.push(line);
  }
  flush();
  return blocks;
}
