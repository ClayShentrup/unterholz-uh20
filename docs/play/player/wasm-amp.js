// Loads an amp engine compiled to WebAssembly (tools/build_web.sh) and wraps its C API:
//   amp_create(sampleRate) -> handle, amp_set_param(handle, id, value), amp_prepare(handle),
//   amp_process(handle, in, outL, outR, n), amp_buffer(handle, 0|1|2), amp_max_block(), amp_snapshot / amp_restore(handle, slot)
// Shared by the AudioWorklet (worklet.js) and the parity test (tools/web_parity.mjs). Amp-agnostic: any engine that
// exports this API plays through the same player.
// SPDX-License-Identifier: AGPL-3.0-or-later

export async function loadAmp(bytes) {
  let mem = null;
  const stub = () => 0;
  const imports = {
    env: { emscripten_notify_memory_growth: stub },
    // the engine never prints or opens files; libc's stdio links these in anyway
    wasi_snapshot_preview1: new Proxy({}, { get: () => stub }),
  };
  // bytes, or an already compiled WebAssembly.Module (whose compiled code is shared with whoever compiled it)
  const r = await WebAssembly.instantiate(bytes, imports);
  const instance = r instanceof WebAssembly.Instance ? r : r.instance;
  const x = instance.exports;
  mem = x.memory;
  if (x._initialize) x._initialize();
  const maxBlock = x.amp_max_block();

  return {
    maxBlock,
    // one amp at this sample rate (the first one also builds the engine's shared tables: the slow part)
    create(sampleRate) {
      const h = x.amp_create(sampleRate);
      const ptr = [x.amp_buffer(h, 0), x.amp_buffer(h, 1), x.amp_buffer(h, 2)];
      let v = null, vMem = null;
      const bufs = () => {   // views over the module's memory, rebuilt if the memory grew
        if (vMem !== mem.buffer) { vMem = mem.buffer; v = ptr.map(p => new Float32Array(vMem, p, maxBlock)); }
        return v;
      };
      return {
        setParam: (id, value) => x.amp_set_param(h, id, value),
        prepare: () => x.amp_prepare(h),
        // the engine's state into a numbered slot and back (amp_snapshot / amp_restore; the buffered player's)
        snapshot: slot => x.amp_snapshot(h, slot),
        restore: slot => !!x.amp_restore(h, slot),
        destroy: () => x.amp_destroy(h),
        // in: Float32Array (mono); outL, outR: Float32Array; n <= maxBlock
        process(input, outL, outR, n) {
          const [bi, bl, br] = bufs();
          bi.set(n === input.length ? input : input.subarray(0, n));
          x.amp_process(h, ptr[0], ptr[1], ptr[2], n);
          const [, l2, r2] = bufs();
          outL.set(n === l2.length ? l2 : l2.subarray(0, n));
          outR.set(n === r2.length ? r2 : r2.subarray(0, n));
        },
      };
    },
  };
}
