#pragma once

#include <fltKernel.h>

namespace NoMoreStealer {  // ← Geändert von NoMoreStealers

    namespace Paths {

        void Init();
        void Cleanup();
        void Add(const WCHAR* path);
        BOOLEAN IsProtected(PUNICODE_STRING filePath);
        void DiscoverDefaultPaths();

    } // namespace Paths
} // namespace NoMoreStealer