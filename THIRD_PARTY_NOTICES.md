# Third-party notices

MHWI-VisualControllerExtended distributes or adapts the following components. These notices must
remain with source and binary distributions.

## SafetyHook v0.7.0 (active backend in the test branch)

Source: https://github.com/cursey/safetyhook/tree/v0.7.0

SafetyHook is distributed under the Boost Software License 1.0. The complete license
is preserved in `third_party/safetyhook/LICENSE.SafetyHook` and must accompany distributions.
The official amalgamated sources are used without edits.

## Zydis v4.1.0 and bundled Zycore

Source: https://github.com/zyantific/zydis/tree/v4.1.0

Zydis and bundled Zycore are distributed under the MIT License. The Zydis license is
preserved in `third_party/safetyhook/LICENSE.Zydis`; the amalgamated sources also retain
the original Zydis and Zycore license notices. Retain these notices in distributions.
The official Zydis v4.1.0 amalgamation replaces the older Zydis included in the SafetyHook
release archive. See `third_party/safetyhook/dependency-lock.json` for provenance and hashes.

## MinHook v1.3.4 (retained source; not linked in the test branch)

Source: https://github.com/TsudaKageyu/minhook/tree/v1.3.4

MinHook - The Minimalistic API Hooking Library for x64/x86  
Copyright (C) 2009-2017 Tsuda Kageyu. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of
   conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other materials provided
   with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The complete MinHook and bundled Hacker Disassembler Engine license text is preserved in
`third_party/minhook/LICENSE.txt` and is incorporated into this notice by reference.

## WeaponSoundEnhance

Source: https://github.com/2749478981/WeaponSoundEnhance/tree/cd87d5db2f6b52ae2ff84a9d6e9108ad24761c6b

Portions of the configuration, logging and guarded memory-reading structure were adapted from
WeaponSoundEnhance under the MIT License.

MIT License  
Copyright (c) 2026 WeaponSoundEnhance contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## mhw-toolkit

Source: https://github.com/eigeen/mhw-toolkit/tree/0a593484976e58358bbb801a821af2f99ed0037e

Player, weapon and chat compatibility data and the chat-message receiver behavior were adapted,
through WeaponSoundEnhance, from mhw-toolkit and modified for this project.

Copyright 2024 Eigeen

Licensed under the Apache License, Version 2.0 (the "License"); you may not use material covered by
that license except in compliance with it. You may obtain a copy at
https://www.apache.org/licenses/LICENSE-2.0.

The complete Apache License 2.0 text and upstream copyright notice are preserved in
`MHW_TOOLKIT_LICENSE.txt` and must accompany source and binary distributions.
