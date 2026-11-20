// Separate bundle (mp3.js): if the MP3 decoder fails to load in a host, the panel still works.
import "./polyfills";  // must come first: the decoder needs TextDecoder at instantiation
import { decodeMp3 } from "../core/mp3";

(globalThis as unknown as { ConductorMp3: unknown }).ConductorMp3 = { decodeMp3 };
