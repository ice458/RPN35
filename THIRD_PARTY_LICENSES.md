# Third-Party Licenses

This project uses third-party software components. The following is a list of these components and their respective licenses:

## Intel Decimal Floating-Point Math Library

Files: `bid_conf.h`, `bid_functions.h`, `gcc111libbid_pico2.a`

Copyright: (c) 2007-2011, Intel Corp. (headers); (c) 2018, Intel Corp. (EULA)

License: BSD 3-Clause License

```
Copyright (c) 2007-2011, Intel Corp.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of Intel Corporation nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
```

Note: The header files `bid_conf.h` and `bid_functions.h` have been lightly modified for Pico compatibility while preserving the original Intel license header. A verbatim copy of Intel's upstream license text is included at `licenses/vendor-intel-dfp-eula.txt` (copied from `IntelRDFPMathLib20U2/eula.txt`, which is ignored by `.gitignore`).

## Raspberry Pi Pico SDK

**Files**: `pico_sdk_import.cmake` and linked libraries

**Copyright**: Copyright (c) 2020 Raspberry Pi (Trading) Ltd.

**License**: BSD 3-Clause License

The Raspberry Pi Pico SDK is licensed under the BSD 3-Clause License. For complete license terms, please refer to the Pico SDK repository.
