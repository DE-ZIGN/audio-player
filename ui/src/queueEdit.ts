// Changing what comes next in the play queue. An edit is of the queue, and
// the current track, it was made for: the app refuses it on others (the track
// can move on, or the queue change, while the edit is on its way).

export interface QueueAt {
  version: number; // of the queue's tracks
  index: number; // the current track
}

export type QueueChange = (ids: number[], current: number) => number[] | null;
export type EditOutcome = "done" | "stale" | "none";

interface QueueApi {
  getStatus(): Promise<{ queueVersion: number; queueIndex: number }>;
  getQueue(): Promise<{ version: number; items: { id: number }[] }>;
  replaceUpcoming(ids: number[], at: QueueAt): Promise<boolean>;
}

const TRIES = 4;

// One edit at a time, each made on the queue as it is then. `drawn`: the
// version of the queue the edit's rows were drawn from; on another it is not
// made ("stale"). Without one (adding, mixing) it is made again on the new one.
export function queueEditor(api: QueueApi): (change: QueueChange, drawn?: number) => Promise<EditOutcome> {
  let last: Promise<unknown> = Promise.resolve();

  async function make(change: QueueChange, drawn?: number): Promise<EditOutcome> {
    for (let i = 0; i < TRIES; i++) {
      const s = await api.getStatus();
      const q = await api.getQueue();
      if (drawn !== undefined && q.version !== drawn) return "stale";
      if (q.version !== s.queueVersion) continue; // it changed between the two
      const ids = q.items.map((item) => item.id);
      if (ids.length === 0 || ids.includes(0)) return "none"; // none, or of opened files: not edited
      const next = change(ids, s.queueIndex);
      if (!next) return drawn === undefined ? "none" : "stale"; // the row has become the current one
      if (await api.replaceUpcoming(next, { version: q.version, index: s.queueIndex })) return "done";
    }
    return "stale";
  }

  return (change, drawn) => {
    const edit = last.then(() => make(change, drawn));
    last = edit.catch(() => undefined);
    return edit;
  };
}

// Whether a double click's two clicks were on one entry: the rows move up
// under the pointer as tracks are removed.
export function sameEntryTwice(first: { version: number; index: number } | null, version: number, index: number): boolean {
  return first?.version === version && first.index === index;
}
