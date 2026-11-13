import { describe, expect, it } from "vitest";
import { rangeFill } from "./rangeFill";

describe("rangeFill (the lit part of a thin slider, task 6-8)", () => {
  it("is the value's place between min and max, in percent", () => {
    expect(rangeFill(0, 0, 1000)).toBe("0%");
    expect(rangeFill(500, 0, 1000)).toBe("50%");
    expect(rangeFill(1000, 0, 1000)).toBe("100%");
    expect(rangeFill(25, 0, 200)).toBe("12.5%");
  });
  it("stays within 0..100 and copes with an empty range", () => {
    expect(rangeFill(-5, 0, 10)).toBe("0%");
    expect(rangeFill(50, 0, 10)).toBe("100%");
    expect(rangeFill(3, 5, 5)).toBe("0%");
    expect(rangeFill(Number.NaN, 0, 10)).toBe("0%");
  });
});
