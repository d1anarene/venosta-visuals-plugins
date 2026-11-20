// Stand-in for @eshaz/web-worker: Conductor uses mpg123's plain decoder, never the web-worker
// variant (UXP has no Worker). Throws if anything ever tries to start one.
export default class NoWorker {
  constructor() {
    throw new Error("web workers are not available in this panel");
  }
}
