// Browser smoke test of the built panel against a MOCKED Premiere API (not a host test).
// Usage: npm run build && NODE_PATH=$(npm root -g) node tests/browser-smoke.cjs
const { chromium } = require('playwright');
const path = require('path');
const dist = process.argv[2] || path.resolve(__dirname, "../dist/premiere-panel");
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 340, height: 1100 } });
  page.on('pageerror', e => console.log('PAGEERROR', e.message));
  const eprFixture = require('fs').readFileSync(path.resolve(__dirname, 'fixtures/sample-h264.epr'), 'utf8');
  await page.addInitScript((eprFixture) => {
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
    // Fake disk: Premiere's system preset folder with one H.264 preset, plus the plugin data folder.
    const APP = '/Applications/Adobe Premiere Pro 2027/Adobe Premiere Pro 2027.app';
    const mkFile = (nativePath, text) => ({ isFile: true, isFolder: false, name: nativePath.split('/').pop(), nativePath, read: async () => text, write: async (t) => { text = t; window.__written[nativePath] = t; } });
    const mkFolder = (nativePath, kids) => ({ isFolder: true, isFile: false, name: nativePath.split('/').pop(), nativePath, getEntries: async () => kids,
      getEntry: async (n) => { const k = kids.find((c) => c.name === n); if (!k) throw new Error('missing'); return k; },
      createFolder: async (n) => { const f = mkFolder(nativePath + '/' + n, []); kids.push(f); return f; },
      createFile: async (n) => { const f = mkFile(nativePath + '/' + n, ''); kids.push(f); return f; } });
    window.__written = {};
    const sysPresets = mkFolder(APP + '/Contents/MediaIO/systempresets', [mkFolder(APP + '/Contents/MediaIO/systempresets/4E49434B_48323634', [mkFile(APP + '/Contents/MediaIO/systempresets/4E49434B_48323634/Match Source - High bitrate.epr', eprFixture)])]);
    const dataFolder = mkFolder('/PluginData', []);
    const byUrl = { ['file:' + sysPresets.nativePath]: sysPresets };
    const uxp = { host: { applicationPath: APP, name: 'premierepro', version: '27.0.0' }, storage: { localFileSystem: {
      getEntryWithUrl: async (u) => { if (byUrl[u]) return byUrl[u]; throw new Error('not found ' + u); },
      getDataFolder: async () => dataFolder,
      getFolder: async () => folder, createPersistentToken: async () => 'tok', getEntryForPersistentToken: async () => folder,
      getFileForOpening: async () => ({ isFile: true, nativePath: '/Presets/ProRes.epr' }),
    } } };
    window.require = (id) => ({ premierepro: ppro, uxp }[id]);
    window.__inout = () => [inT, outT];
  }, eprFixture);
  await page.goto('file://' + path.resolve(dist, 'index.html'));
  await page.waitForTimeout(300);
  await page.click('#btn-scan');
  await page.waitForTimeout(300);
  console.log('ROWS', await page.$$eval('.job', els => els.map(e => e.querySelector('.clip').textContent + ' | ' + e.querySelector('.times').textContent)));
  await page.click('#btn-folder');
  console.log('DEST INFO', await page.textContent('#dest-info'));
  await page.waitForTimeout(200);
  await page.click('#btn-queue');
  await page.waitForTimeout(800);
  console.log('QUEUED', JSON.stringify(await page.evaluate(() => window.__log), null, 0));
  console.log('INOUT AFTER', await page.evaluate(() => window.__inout()));
  const written = await page.evaluate(() => window.__written);
  for (const [p, xml] of Object.entries(written)) {
    const get = (id) => (xml.match(new RegExp('<ParamValue>([^<]*)</ParamValue>\\s*<ParamType>\\d</ParamType>[\\s\\S]{0,80}?<ParamIdentifier>' + id + '<')) || [])[1];
    console.log('PRESET', p.split('/').pop(), 'w', get('ADBEVideoWidth'), 'h', get('ADBEVideoHeight'), 'fps', get('ADBEVideoFPS'), 'br', get('ADBEVideoTargetBitrate'), 'audio', /<DoAudio>true/.test(xml));
  }
  console.log('SUMMARY', await page.textContent('#summary'));
  console.log('MESSAGES', await page.$$eval('.msg', els => els.map(e => e.textContent)));
  await page.screenshot({ path: process.env.SHOT || 'panel-smoke.png', fullPage: true });
  await browser.close();
})();
