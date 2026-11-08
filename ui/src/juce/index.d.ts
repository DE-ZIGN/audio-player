// Type declarations for JUCE's frontend helper (index.js, copied from
// juce_gui_extra/native/javascript; keep in sync when JUCE is updated).
export function getNativeFunction(name: string): (...args: unknown[]) => Promise<unknown>;
export function getBackendResourceAddress(path: string): string;
