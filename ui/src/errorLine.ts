// The page's one error line: the playback's error, and what went wrong in an
// action here. The status comes up to 10 times a second: an error it already
// had does not take the line back from an action's message; a new one does.

export type ErrorShow = "status" | "keep" | "hide";

// `from`: whose message the line shows; `before`, `now`: the playback's error
// (which one it is, whatever the language), null when none.
export function errorLine(from: string, before: string | null, now: string | null): ErrorShow {
  if (now !== null) return from === "action" && before === now ? "keep" : "status";
  return from === "status" ? "hide" : "keep";
}
