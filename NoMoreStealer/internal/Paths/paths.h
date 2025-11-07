#pragma once

#include <fltKernel.h>

namespace NoMoreStealer {

	namespace Paths {

		void Init();
		void Cleanup();
		void Add(const WCHAR* path);
		BOOLEAN IsProtected(PUNICODE_STRING filePath);
		void DiscoverDefaultPaths();

	}

}


