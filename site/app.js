// Patches the player's own Emerald file in the browser, keeps the result in IndexedDB so
// they only pick the file once, and starts it in EmulatorJS.
"use strict";

const PATCH_URL = "daily-pokemon-battle.bps";
const DB_NAME = "daily-pokemon-battle";
const GAME_NAME = "Daily Pokemon Battle"; // EmulatorJS keys the save file on this

const $ = (id) => document.getElementById(id);

function status(text, isError = false) {
  $("status").textContent = text;
  $("status").classList.toggle("error", isError);
}

function db() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => req.result.createObjectStore("files");
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

async function store(mode, fn) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const tx = d.transaction("files", mode);
    const req = fn(tx.objectStore("files"));
    tx.oncomplete = () => resolve(req && req.result);
    tx.onerror = () => reject(tx.error);
  });
}

const getRom = () => store("readonly", (s) => s.get("rom")).catch(() => null);
const putRom = (rom) => store("readwrite", (s) => s.put(rom, "rom")).catch(() => {});
const dropRom = () => store("readwrite", (s) => s.delete("rom")).catch(() => {});

// A patched ROM from an older release has a different CRC; drop it so the player repatches.
async function cachedRom(patch) {
  const saved = await getRom();
  if (!saved) return null;
  const want = new DataView(patch.buffer).getUint32(patch.length - 8, true);
  if (saved.crc === want) return saved.bytes;
  await dropRom();
  return null;
}

let patchPromise;
function loadPatch() {
  patchPromise ??= fetch(PATCH_URL).then((r) => {
    if (!r.ok) throw new Error("Couldn't download the patch (" + r.status + ").");
    return r.arrayBuffer();
  }).then((b) => new Uint8Array(b));
  return patchPromise;
}

function start(rom) {
  $("setup").hidden = true;
  $("game-wrap").hidden = false;
  window.EJS_player = "#game";
  window.EJS_core = "gba";
  window.EJS_gameName = GAME_NAME;
  window.EJS_gameUrl = URL.createObjectURL(new Blob([rom], { type: "application/octet-stream" }));
  window.EJS_pathtodata = "emulatorjs/data/";
  window.EJS_DEBUG_XX = true; // load the unminified scripts we ship
  window.EJS_startOnLoaded = true;
  window.EJS_color = "#e2463d";
  window.EJS_backgroundColor = "#000";
  window.EJS_disableAutoLang = true;
  window.EJS_defaultOptions = { "save-save-interval": "30" };
  const s = document.createElement("script");
  s.src = "emulatorjs/data/loader.js";
  document.body.appendChild(s);
  $("game-wrap").scrollIntoView({ behavior: "smooth" });
}

async function onFile(file) {
  try {
    status("Patching…");
    const [patch, base] = await Promise.all([loadPatch(), file.arrayBuffer()]);
    let rom;
    try {
      rom = applyBps(new Uint8Array(base), patch);
    } catch (e) {
      if (/right base/.test(e.message)) {
        throw new Error("That file isn't a clean Pokémon Emerald (USA/Europe). Check it's the original .gba, not a romhack or another region.");
      }
      throw e;
    }
    await putRom({ crc: crc32(rom), bytes: rom });
    status("");
    start(rom);
  } catch (e) {
    status(e.message, true);
  }
}

async function init() {
  $("rom").addEventListener("change", (e) => e.target.files[0] && onFile(e.target.files[0]));
  $("forget").addEventListener("click", async () => {
    await dropRom();
    $("have-rom").hidden = true;
    $("need-rom").hidden = false;
  });
  try {
    const rom = await cachedRom(await loadPatch());
    if (rom) {
      $("have-rom").hidden = false;
      $("need-rom").hidden = true;
      $("continue").addEventListener("click", () => start(rom));
    }
  } catch (e) {
    status(e.message, true);
  }
}

init();
