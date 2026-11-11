// Browser smoke test of the built panel against a MOCKED Premiere API (not a host test).
// Usage: npm run build && NODE_PATH=$(npm root -g) node tests/browser-smoke.cjs
const { chromium } = require('playwright');
const path = require('path');
const dist = process.argv[2] || path.resolve(__dirname, "../dist/premiere-panel");
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 340, height: 1100 } });
  page.on('pageerror', e => console.log('PAGEERROR', e.message));
  await page.addInitScript(() => {
    // Simulate APIs missing in Premiere's UXP runtime (found in host testing).
    delete window.TextEncoder; delete window.TextDecoder; Element.prototype.toggleAttribute = undefined;
    const TPF = 10160640000; // 25 fps
    const T = (f) => ({ ticks: String(f * TPF), seconds: f / 25 });
    const log = (window.__log = []);
    let inT = '0', outT = String(1000 * TPF);
    const mkItem = (name, s, e) => ({ getStartTime: async () => T(s), getEndTime: async () => T(e), getName: async () => name, isDisabled: async () => false, isAdjustmentLayer: async () => false });
    const v1 = { name: 'Main', isMuted: async () => false, getTrackItems: (type) => type === 1 ? [mkItem('B.mov', 200, 300), mkItem('A.mov', 0, 125), mkItem('C.mov', 300, 350)] : [] };
    const v3 = { name: 'Look', isMuted: async () => false, getTrackItems: () => [mkItem('Adjustment Layer', 0, 350)] };
    const seq = {
      name: 'Graded Sequence', guid: { toString: () => 'seq-1' },
      getVideoTrackCount: async () => 3, getVideoTrack: async (i) => [v1, { name: '', isMuted: async () => false, getTrackItems: () => [] }, v3][i],
      getSettings: async () => ({ getVideoFrameRate: () => ({ ticksPerFrame: TPF }), getVideoDisplayFormat: async () => ({ type: 0 }) }),
      getZeroPoint: async () => T(0), getTimebase: async () => String(TPF),
      getInPoint: async () => ({ ticks: inT }), getOutPoint: async () => ({ ticks: outT }),
      createSetInPointAction: (t) => ({ run: () => (inT = t.ticks) }),
      createSetOutPointAction: (t) => ({ run: () => (outT = t.ticks) }),
    };
    const project = {
      path: '/Projects/test.prproj', name: 'test', getActiveSequence: async () => seq, getSequences: async () => [seq], lockedAccess: (cb) => cb(), save: async () => true,
      executeTransaction: (cb) => { const acts = []; cb({ addAction: (a) => acts.push(a) }); acts.forEach(a => a.run()); return true; },
    };
    const ppro = {
      Project: { getActiveProject: async () => project },
      Constants: { TrackItemType: { EMPTY: 0, CLIP: 1, TRANSITION: 2 }, ExportType: { QUEUE_TO_AME: 0 } },
      SequenceSettings: { VIDEO_DISPLAY_FORMAT_2997: 99 },
      TickTime: { createWithTicks: (t) => ({ ticks: t }) },
      EncoderManager: {
        getExportFileExtension: async () => 'mov',
        getManager: () => ({ isAMEInstalled: true, exportSequence: async (s, type, out, preset, full) => { log.push({ out, preset, full, inT, outT }); return true; } }),
      },
    };
    const folder = { isFolder: true, nativePath: '/Exports', name: 'Exports', getEntries: async () => [{ name: '001_A.mov' }] };
    const uxp = { storage: { localFileSystem: {
      getFolder: async () => folder, createPersistentToken: async () => 'tok', getEntryForPersistentToken: async () => folder,
      getFileForOpening: async () => ({ isFile: true, nativePath: '/Presets/ProRes.epr' }),
    } } };
    window.require = (id) => ({ premierepro: ppro, uxp }[id]);
    window.__inout = () => [inT, outT];
  });
  await page.goto('file://' + path.resolve(dist, 'index.html'));
  await page.waitForTimeout(300);
  await page.click('#btn-scan');
  await page.waitForTimeout(300);
  console.log('ROWS', await page.$$eval('.job', els => els.map(e => e.querySelector('.clip').textContent + ' | ' + e.querySelector('.times').textContent)));
  await page.click('#btn-folder'); await page.click('#btn-preset');
  await page.waitForTimeout(200);
  await page.click('#btn-queue');
  await page.waitForTimeout(800);
  console.log('QUEUED', JSON.stringify(await page.evaluate(() => window.__log), null, 0));
  console.log('INOUT AFTER', await page.evaluate(() => window.__inout()));
  console.log('SUMMARY', await page.textContent('#summary'));
  console.log('MESSAGES', await page.$$eval('.msg', els => els.map(e => e.textContent)));
  await page.screenshot({ path: process.env.SHOT || 'panel-smoke.png', fullPage: true });
  await browser.close();
})();
