#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

namespace SSXTest
{
	// A test (or part of one) that could not run because reference data, recordings or SSX_ORIGINAL_REPLAY are
	// missing. tools/test_summary.py counts a test with this marker as skipped, not passed.
	inline void Skip(FAutomationTestBase& Test, const FString& Reason)
	{
		Test.AddInfo(FString::Printf(TEXT("[SSX-SKIP] %s"), *Reason));
	}
}
