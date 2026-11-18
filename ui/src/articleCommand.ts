// Task 7-1: what a link in an article may ask of the main screen: to open a
// screen. Nothing else: no setting is changed and nothing is played.
// The app checks the command first (app/logic/ArticlePolicy.h); this is the
// same list on the page's side.

// The settings screen's headings (index.html, data-section), in order.
export const ARTICLE_SETTINGS_SECTIONS = [
  "output",
  "mode",
  "isp-safe",
  "replaygain",
  "eq",
  "tone",
  "tube",
  "space",
  "crossfeed",
  "output-rate",
  "look",
] as const;

export type ArticleScreen =
  | { screen: "abx" }
  | { screen: "output-check" }
  | { screen: "settings"; section: string | null };

export function articleScreen(payload: unknown): ArticleScreen | null {
  if (typeof payload !== "object" || payload === null) return null;
  const { command, argument = "" } = payload as { command?: unknown; argument?: unknown };
  if (typeof command !== "string" || typeof argument !== "string") return null;
  if (command === "abx" || command === "output-check") return argument === "" ? { screen: command } : null;
  if (command !== "settings") return null;
  if (argument === "") return { screen: "settings", section: null };
  return (ARTICLE_SETTINGS_SECTIONS as readonly string[]).includes(argument) ? { screen: "settings", section: argument } : null;
}
