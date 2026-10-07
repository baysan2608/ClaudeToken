// Fourfold - dev capture triggers (see FourfoldModule.cpp): gameplay code calls Trigger("<tag>"); with
// -FFBurst=<tag>:<count>:<interval>[,...] on the command line that starts a burst of viewport screenshots
// (-FFShotDir=<folder>, files <tag>_<n>_<i>.png); -FFBurstQuit=<n> quits after n bursts. No-op otherwise.
#pragma once

#include "CoreMinimal.h"

namespace FourfoldDev
{
	void Trigger(const TCHAR* Tag);
}
