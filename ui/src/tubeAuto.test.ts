import { describe, expect, it } from "vitest";
import {
  chooseTubeType,
  choiceText,
  normalizedGenre,
  rulesProblem,
  withAlbumChoice,
  type TubeAutoSettings,
} from "./tubeAuto";

const settings = (over: Partial<TubeAutoSettings> = {}): TubeAutoSettings => ({
  enabled: true,
  rules: [
    { genre: "Rock", type: "ppel34" },
    { genre: "Jazz", type: "se300b" },
    { genre: "Jazz Rock", type: "ppclassa" },
  ],
  albums: [],
  ...over,
});

// The same cases as the engine's (tests/unit/tube_auto_test.cpp).
describe("chooseTubeType (the page's copy, for the mock)", () => {
  it("chooses nothing when off", () => {
    expect(chooseTubeType(settings({ enabled: false }), ["Jazz"], "")).toBeNull();
  });
  it("matches ignoring case, width and spaces", () => {
    expect(chooseTubeType(settings(), ["  jazz "], "")).toEqual({ type: "se300b", reason: "genre", matched: "jazz" });
    expect(chooseTubeType(settings(), ["Ｊａｚｚ"], "")?.type).toBe("se300b");
    expect(chooseTubeType(settings(), ["Reggae"], "")).toBeNull();
    expect(chooseTubeType(settings(), [], "")).toBeNull();
  });
  it("takes exact matches first, then a table genre inside the tag's", () => {
    expect(chooseTubeType(settings(), ["Jazz Rock"], "")?.type).toBe("ppclassa");
    expect(chooseTubeType(settings(), ["Vocal Jazz"], "")?.type).toBe("se300b");
    expect(chooseTubeType(settings(), ["Rock and Jazz"], "")?.type).toBe("ppel34");
    expect(chooseTubeType(settings(), ["Jazz; Rock"], "")?.type).toBe("se300b");
    expect(chooseTubeType(settings(), ["Rock/Jazz"], "")?.type).toBe("ppel34");
    expect(chooseTubeType(settings(), ["Hard Rock Jazz", "Jazz Rock"], "")?.type).toBe("ppclassa");
  });
  it("puts an album's choice before the genre", () => {
    const s = settings({ albums: [{ albumKey: "k", album: "Waltz for Debby", type: "ppclassa" }] });
    expect(chooseTubeType(s, ["Jazz"], "k")).toEqual({ type: "ppclassa", reason: "album", matched: "Waltz for Debby" });
    expect(chooseTubeType(s, ["Jazz"], "other")?.type).toBe("se300b");
  });
});

describe("the album's choice", () => {
  it("is set, replaced and removed", () => {
    const album = { key: "k", title: "Waltz for Debby" };
    let s = withAlbumChoice(settings(), album, "se300b");
    expect(s.albums).toEqual([{ albumKey: "k", album: "Waltz for Debby", type: "se300b" }]);
    s = withAlbumChoice(s, album, "ppel34");
    expect(s.albums).toEqual([{ albumKey: "k", album: "Waltz for Debby", type: "ppel34" }]);
    expect(withAlbumChoice(s, album, null).albums).toEqual([]);
  });
});

describe("the table's problems", () => {
  it("finds empty and repeated genres", () => {
    expect(rulesProblem(settings().rules)).toBeNull();
    expect(rulesProblem([{ genre: "  ", type: "se300b" }])).toBe("ジャンルが空の行があります");
    expect(
      rulesProblem([
        { genre: "Jazz", type: "se300b" },
        { genre: "JAZZ ", type: "ppel34" },
      ]),
    ).toBe("「Jazz」と「JAZZ」は同じジャンルとして扱われます");
  });
  it("compares as the engine does", () => {
    expect(normalizedGenre("  Ｒｏｃｋ　 Pop ")).toBe("rock pop");
    expect(normalizedGenre("ジャズ")).toBe("ジャズ");
  });
});

describe("the line about the current track", () => {
  const s = settings();
  it("says why the type plays", () => {
    expect(choiceText({ ...s, enabled: false }, null, "se300b")).toBe("自動で選びません。いつも手動で選んだ種類（SE 300B）です。");
    expect(
      choiceText(s, { album: null, genres: ["Jazz"], choice: { type: "se300b", reason: "genre", matched: "Jazz" } }, "ppel34"),
    ).toBe("今の曲：ジャンル「Jazz」が対応表にあるので SE 300B。");
    expect(
      choiceText(
        s,
        { album: { key: "k", title: "W" }, genres: ["Jazz"], choice: { type: "ppclassa", reason: "album", matched: "W" } },
        "se300b",
      ),
    ).toBe("今の曲：アルバム「W」の指定で PP CLASS A。");
    expect(choiceText(s, { album: null, genres: ["Reggae", "Dub"], choice: null }, "ppel34")).toBe(
      "今の曲：ジャンル「Reggae・Dub」は対応表にないので、手動で選んだ種類（PP EL34）。",
    );
    expect(choiceText(s, { album: null, genres: [], choice: null }, "ppel34")).toBe(
      "今の曲：ジャンルのタグがないので、手動で選んだ種類（PP EL34）。",
    );
  });
});
