#pragma once
// Forward to the generated ppc_context.h in BlurRecompLib
#include "../../BlurRecompLib/ppc/ppc_context.h"

// Blur Recomp (2026-11-16): current-thread PPC context accessors.
// The template's code uses them everywhere (kernel/function.h,
// cpu/guest_stack_var.h, the GuestThreadContext constructor...) but
// SanRecomp's snapshot never declared or defined them. Declared here
// because every consumer includes this header directly or through
// kernel/function.h; defined in cpu/guest_thread.cpp.
PPCContext* GetPPCContext();
void SetPPCContext(PPCContext& context);
