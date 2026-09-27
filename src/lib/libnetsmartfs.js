/**
 * @license
 * Copyright 2023 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

addToLibrary({
  $NETSMARTFS__deps: ['$stringToUTF8OnStack', 'wasmfs_create_netsmart_backend'],
  $NETSMARTFS: {
    createBackend(opts) {
      console.log("Creating netsmart backend with", opts);
      return withStackSave(
        () => _wasmfs_create_netsmart_backend(
          stringToUTF8OnStack(opts.base_url ?? ''),
          opts.chunkSize | 0
        )
      );
    },
  },
});

if (!WASMFS) {
  error('using -lnetsmartfs.js requires using WasmFS (-sWASMFS)');
}
