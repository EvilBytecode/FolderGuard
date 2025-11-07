#pragma once

#include <fltKernel.h>

namespace NoMoreStealer {

	namespace Callbacks {

		FLT_PREOP_CALLBACK_STATUS __stdcall PreOperation(
			PFLT_CALLBACK_DATA Data,
			PCFLT_RELATED_OBJECTS FltObjects,
			PVOID* CompletionContext
		);

	}

}


