import { describe, expect, it } from "vitest";
import { MockBackend } from "./backend";
import { queueEditor, sameEntryTwice, type QueueAt } from "./queueEdit";
import { afterInsert, afterRemove } from "./queueOps";
import main from "./main.ts?raw";

// An edit of the play queue is of the queue, and the track, it was made for
// (found 2026-11-22: the removed came back, a track twice in the queue, and
// the x jumped the playback).

// The app's queue: it takes an edit only at the version and index it is made for.
function app(items = [1, 2, 3, 4, 5, 6, 7], index = 0) {
  const q = { version: 1, index, items: [...items] };
  let beforeReplace: (() => void) | undefined;
  return {
    q,
    onReplace: (f: () => void) => (beforeReplace = f),
    api: {
      getStatus: async () => ({ queueVersion: q.version, queueIndex: q.index }),
      getQueue: async () => ({ version: q.version, items: q.items.map((id) => ({ id })) }),
      replaceUpcoming: async (ids: number[], at: QueueAt) => {
        beforeReplace?.();
        beforeReplace = undefined;
        if (at.version !== q.version || at.index !== q.index) return false;
        q.items = [...q.items.slice(0, q.index + 1), ...ids];
        q.version++;
        return true;
      },
    },
  };
}
const remove = (i: number) => (ids: number[], current: number) => afterRemove(ids, current, i);

describe("editing what comes next", () => {
  it("removes from the queue as drawn", async () => {
    const { q, api } = app();
    expect(await queueEditor(api)(remove(2), 1)).toBe("done");
    expect(q.items).toEqual([1, 2, 4, 5, 6, 7]);
  });

  it("does not apply a row's edit to a queue that has changed since it was drawn", async () => {
    const { q, api } = app();
    q.version = 2;
    expect(await queueEditor(api)(remove(2), 1)).toBe("stale");
    expect(q.items).toEqual([1, 2, 3, 4, 5, 6, 7]);
  });

  it("two at once: the first holds, the second is refused, nothing comes back", async () => {
    const { q, api } = app();
    const edit = queueEditor(api);
    expect(await Promise.all([edit(remove(2), 1), edit(remove(4), 1)])).toEqual(["done", "stale"]);
    expect(q.items).toEqual([1, 2, 4, 5, 6, 7]);
  });

  it("as the track moves on: made again from the new current one, none twice", async () => {
    const a = app();
    a.onReplace(() => (a.q.index = 1));
    expect(await queueEditor(a.api)(remove(4), 1)).toBe("done");
    expect(a.q.items).toEqual([1, 2, 3, 4, 6, 7]);
  });

  it("does not remove the one that has just started", async () => {
    const a = app();
    a.onReplace(() => (a.q.index = 1));
    expect(await queueEditor(a.api)(remove(1), 1)).toBe("stale");
    expect(a.q.items).toEqual([1, 2, 3, 4, 5, 6, 7]);
  });

  it("adds twice in a row: both are there", async () => {
    const { q, api } = app([1, 2]);
    const edit = queueEditor(api);
    const add = (id: number) => (ids: number[], current: number) => afterInsert(ids, current, [id], "end");
    expect(await Promise.all([edit(add(8)), edit(add(9))])).toEqual(["done", "done"]);
    expect(q.items).toEqual([1, 2, 8, 9]);
  });

  it("makes an addition again on a queue changed meanwhile", async () => {
    const a = app([1, 2]);
    a.onReplace(() => {
      a.q.items = [5, 6];
      a.q.version++;
    });
    expect(await queueEditor(a.api)((ids, current) => afterInsert(ids, current, [9], "end"))).toBe("done");
    expect(a.q.items).toEqual([5, 6, 9]);
  });

  it("leaves a queue of opened files as it is", async () => {
    const { q, api } = app([0, 0, 0]);
    expect(await queueEditor(api)(() => [])).toBe("none");
    expect(q.items).toEqual([0, 0, 0]);
  });

  it("goes on after an edit that failed", async () => {
    const { q, api } = app();
    const edit = queueEditor(api);
    await expect(
      edit(() => {
        throw new Error("no");
      }),
    ).rejects.toThrow("no");
    expect(await edit(remove(2), 1)).toBe("done");
    expect(q.version).toBe(2);
  });
});

describe("a double click on the queue", () => {
  it("plays when both clicks were on one entry", () => {
    expect(sameEntryTwice({ version: 3, index: 2 }, 3, 2)).toBe(true);
  });

  it("does not when the list changed between them, or the first was on an x", () => {
    expect(sameEntryTwice({ version: 3, index: 2 }, 4, 2)).toBe(false);
    expect(sameEntryTwice({ version: 3, index: 2 }, 3, 1)).toBe(false);
    expect(sameEntryTwice(null, 3, 2)).toBe(false);
  });
});

describe("the mock app", () => {
  it("takes an edit or a jump only for the queue it is made for", async () => {
    const b = new MockBackend();
    await b.openFile();
    const { version } = await b.getQueue();
    const { queueIndex: index } = await b.getStatus();
    expect(await b.playAt(0, { version: version + 1 })).toBe(false);
    expect(await b.playAt(0, { version, index: index + 1 })).toBe(false);
    expect(await b.library.replaceUpcoming([], { version, index: index + 1 })).toBe(false);
    expect(await b.library.replaceUpcoming([], { version: version + 1, index })).toBe(false);
    expect((await b.getStatus()).queueIndex).toBe(index);
    expect(await b.playAt(0, { version, index })).toBe(true);
  });
});

// There is no DOM in these tests, so the wiring is read from the source.
describe("the queue's rows", () => {
  it("the x keeps its double click from the row", () => {
    expect(main).toMatch(/remove\.addEventListener\("dblclick", \(e\) => e\.stopPropagation\(\)\)/);
  });

  it("a row plays only for two clicks on that entry, in the queue it was drawn from", () => {
    expect(main).toMatch(/sameEntryTwice\(queueClicked, version, i\)/);
    expect(main).toMatch(/backend\.playAt\(i, \{ version \}\)/);
  });

  it("every edit goes through the editor", () => {
    expect(main.match(/replaceUpcoming\(/g) ?? []).toHaveLength(1);
  });
});
