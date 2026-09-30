#include "MRSuitConfiguration.h"

#include "Engine/StaticMesh.h"

float UMRSuitConfiguration::GetSuitHeight() const
{
	return SuitMesh ? static_cast<float>(SuitMesh->GetBoundingBox().GetSize().Z) * SuitScale : 0.f;
}
