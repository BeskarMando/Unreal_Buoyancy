/*=================================================
* FileName: NetworkedBuoyantPawnMovementComponent.cpp
*
* Created by: Tobias Moos
* Project name: Sails of War / OceanProject
* Unreal Engine version: 4.19
* Created on: 2020/01/08
*
* Last Edited on: 2020/03/6
* Last Edited by: Tobias Moos
*
* -------------------------------------------------
* Created for: Sails Of War - http://sailsofwargame.com/
* -------------------------------------------------
* For parts referencing UE4 code, the following copyright applies:
* Copyright 1998-2020 Epic Games, Inc. All Rights Reserved.
*
* Feel free to use this software in any commercial/free game.
* Selling this as a plugin/item, in whole or part, is not allowed.
* See "OceanProject\License.md" for full licensing details.
* =================================================*/
//Libary Includes:
#include "NetworkedBuoyantPawnMovementComponent.h"
#include "NetworkedBuoyantPawn.h"
#include "BuoyantMeshComponent.h"

// TODO FIX ME!
//Project Includes:
#include "SOWGameplayStatics.h"
#include "SOWOceanActor.h"
#include "SOWGameState.h"

//Engine Includes: 
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "UnitConversion.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsMovementReplication.h"
#include "UnrealNetwork.h"

#define PrintWarning(Text) if(GEngine) GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Red, Text)
#define PrintMessage(Text) if(GEngine) GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Green, Text)
#define LogWarning(Text) UE_LOG(LogTemp, Warning, TEXT(Text))

DECLARE_CYCLE_STAT(TEXT("UpdateWaterGrid"), STAT_WaterGrid, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("UpdateBuoyantMeshData"), STAT_UpdateBuoyantMeshData, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("CutBuoyantTriangle"), STAT_CutPerTri, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("SplitBuoyantTriangle"), STAT_SplitPerTri, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("CalculateAndApplyWaterEntryForce"), STAT_WaterEntryForce, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("ApplyBuoyantForcesFromTriangle"), STAT_ApplyForcesPerTri, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("CalculateTrianglesForces"), STAT_CalcForcesPerTri, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("BuoyantMeshData Creation"), STAT_BuoyantMeshDataCreation, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("PhysicsSubstep"), STAT_Substep, STATGROUP_BuoyancyPhysics);
DECLARE_CYCLE_STAT(TEXT("MovementSubStep"), STAT_MovementSubStep, STATGROUP_PhysicsMovement);

UNetworkedBuoyantPawnMovementComponent::UNetworkedBuoyantPawnMovementComponent()
{
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.EndTickGroup = TG_PostPhysics;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	SetComponentTickEnabled(true);
	bAutoActivate = true;
	bWantsInitializeComponent = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bReplicates = true;
}

void UNetworkedBuoyantPawnMovementComponent::CreateBuoyantData(class UBuoyantMeshComponent* NewBuoyantMesh, FWaterGrid& OutWaterGrid, FBuoyancyData& OutBuoyancyData, FBodyInstance* OutBodyInstance, float CellSize, FVector OwnerLocation)
{
	int32 NumLODs = NewBuoyantMesh->GetStaticMesh()->RenderData->LODResources.Num();
	FStaticMeshLODResources& LODResource = NewBuoyantMesh->GetStaticMesh()->RenderData->LODResources[NumLODs - 1];
	int numIndices = LODResource.IndexBuffer.IndexBufferRHI->GetSize() / sizeof(uint16);
	uint16* Indices = new uint16[numIndices];
	int numVertices = LODResource.VertexBuffers.PositionVertexBuffer.VertexBufferRHI->GetSize() / (sizeof(float) * 3);
	float* Vertices = new float[numVertices * 3];

	FRawStaticIndexBuffer* IndexBuffer = &LODResource.IndexBuffer;
	uint16* Indices0 = Indices;
	FPositionVertexBuffer*  PositionVertexBuffer = &LODResource.VertexBuffers.PositionVertexBuffer;
	float* Vertices0 = Vertices;
	ENQUEUE_RENDER_COMMAND(GetMyBuffers)
		(
			[IndexBuffer, Indices0, PositionVertexBuffer, Vertices0](FRHICommandListImmediate& RHICmdList)

	{
		uint16* indices1 = (uint16*)RHILockIndexBuffer(IndexBuffer->IndexBufferRHI, 0, IndexBuffer->IndexBufferRHI->GetSize(), RLM_ReadOnly);
		float* indices2 = (float*)RHILockVertexBuffer(PositionVertexBuffer->VertexBufferRHI, 0, PositionVertexBuffer->VertexBufferRHI->GetSize(), RLM_ReadOnly);

		memcpy(Indices0, indices1, IndexBuffer->IndexBufferRHI->GetSize());
		memcpy(Vertices0, indices2, PositionVertexBuffer->VertexBufferRHI->GetSize());

		RHIUnlockIndexBuffer(IndexBuffer->IndexBufferRHI);
		RHIUnlockVertexBuffer(PositionVertexBuffer->VertexBufferRHI);
	}
	);

	FlushRenderingCommands();

	int32 NumVerts = LODResource.GetNumVertices();
	int32 NumTris = LODResource.GetNumTriangles();
	FPositionVertexBuffer* PosVertexBuffer = &LODResource.VertexBuffers.PositionVertexBuffer;
	FIndexArrayView IndexBufferArray = LODResource.IndexBuffer.GetArrayView();
	uint32 Stride = LODResource.VertexBuffers.PositionVertexBuffer.GetStride();

	uint8* VertexBufferContent = (uint8*)Vertices;

	TArray<FMeshVertex>RawMeshVertices;
	TArray<FMeshTriangle>RawMeshTriangles;
	for (int32 TriIndex = 0; TriIndex < NumTris; TriIndex++)
	{
		int32 ia = Indices[TriIndex * 3 + 0];
		int32 ib = Indices[TriIndex * 3 + 1];
		int32 ic = Indices[TriIndex * 3 + 2];

		FVector va = ((FPositionVertex*)(VertexBufferContent + ia * Stride))->Position;
		FVector vb = ((FPositionVertex*)(VertexBufferContent + ib * Stride))->Position;
		FVector vc = ((FPositionVertex*)(VertexBufferContent + ic * Stride))->Position;

		FMeshVertex MVertA = FMeshVertex(va);
		FMeshVertex MVertB = FMeshVertex(vb);
		FMeshVertex MVertC = FMeshVertex(vc);
		RawMeshVertices.Add(MVertA);
		RawMeshVertices.Add(MVertB);
		RawMeshVertices.Add(MVertC);
	}

	FMeshData MeshData = FMeshData(RawMeshVertices);
	OutBuoyancyData = FBuoyancyData(16, MeshData);
	OutWaterGrid = FWaterGrid(CellSize, NewBuoyantMesh->GetStaticMesh()->GetBoundingBox().GetSize(), OwnerLocation);
	OutBodyInstance->SetMaxAngularVelocityInRadians(BuoyancyInformation.PhysicsOverrides.MaxAngularVelocity, true, true);
	OutBodyInstance->AngularDamping = BuoyancyInformation.PhysicsOverrides.AngularDamping;
	OutBodyInstance->COMNudge = BuoyancyInformation.PhysicsOverrides.CenterOfMassNudgeOffset;
	OutBodyInstance->UpdateMassProperties();
	OutBodyInstance->UpdateDampingProperties();
}

void UNetworkedBuoyantPawnMovementComponent::SetBuoyantMesh(class UBuoyantMeshComponent* NewBuoyantMesh)
{
	if (BuoyantMesh != NewBuoyantMesh)
	{
		BuoyantMesh = NewBuoyantMesh;
		if (BuoyantMesh->GetStaticMesh() != nullptr)
			CreateBuoyantData(BuoyantMesh, WaterGrid, BuoyancyData, BuoyantMesh->GetBodyInstance(), BuoyancyInformation.WaterGridCellSize, GetOwner()->GetActorLocation());
	}

	OceanActor = USOWGameplayStatics::GetOceanActor(GetWorld());
}

void UNetworkedBuoyantPawnMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ANetworkedBuoyantPawn* Pawn = Cast<ANetworkedBuoyantPawn>(GetOwner());
	if (Pawn)
	{
		if (Pawn->IsLocallyControlled())
			DrawBuoyantDebug();
	}
}

void UNetworkedBuoyantPawnMovementComponent::PhysicsSubstep(float DeltaSubstepTime, FBodyInstance* BodyInstance)
{
	ANetworkedBuoyantPawn* Pawn = Cast<ANetworkedBuoyantPawn>(GetOwner());
	if (Pawn)
	{
		//Only update if the pawn is the auth. client - supports listen servers
		if (Pawn->Role == ROLE_AutonomousProxy || (Pawn->Role == ROLE_Authority && Pawn->IsLocallyControlled()))
		{
			SCOPE_CYCLE_COUNTER(STAT_Substep);
			{
				BuoyancyData.SubFrameCircularBuffer[0] = BuoyancyData.SubFrameCircularBuffer[1];
				BuoyancyData.SubFrameCircularBuffer[1].Clear();
				BodyInstanceTransform = BodyInstance->GetUnrealWorldTransform_AssumesLocked();
				UpdateWaterGrid(DeltaSubstepTime, BodyInstance);
				UpdateBuoyantMeshData(DeltaSubstepTime, BodyInstance);
				PerformMovement(DeltaSubstepTime, BodyInstance);
			}
		}	
	}
}

void UNetworkedBuoyantPawnMovementComponent::UpdateWaterGrid(float SubstepDeltaTime, FBodyInstance* BodyInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_WaterGrid);
	{
		UWorld* World = GetWorld();
		if (!BodyInstance->GetBodyBounds().IsInside(WaterGrid.GetWaterGridBoundingBox())) //We shouldn't need to transform the bounding box by the body's rotation
			WaterGrid = FWaterGrid(BuoyancyInformation.WaterGridCellSize, BuoyantMesh->GetStaticMesh()->GetBoundingBox().GetSize(), BodyInstanceTransform.GetLocation());
		
		//IMPORT_TASK: Change to AGameState instead
		//UPDATE_TASK: Correctly update the server time to prevent drift by overriding GetServerWorldTimeSeconds()
		ASOWGameState* SOWGS = GetWorld()->GetGameState<ASOWGameState>();
		if (SOWGS)
		{
			for (int PRow = 0; PRow < WaterGrid.Vertices.Num(); PRow++)
			{
				for (int PCol = 0; PCol < WaterGrid.Vertices[PRow].Num(); PCol++)
				{
					FVector TempVector = OceanActor->GetOceanOffset(WaterGrid.Vertices[PRow][PCol].Vertex, SOWGS->GetServerWorldTimeSeconds());
					WaterGrid.Vertices[PRow][PCol].Vertex.Z = OceanActor->GetOceanHeight(WaterGrid.Vertices[PRow][PCol].Vertex - TempVector, SOWGS->GetServerWorldTimeSeconds());
				}
			}
		}	
	}
}

void UNetworkedBuoyantPawnMovementComponent::UpdateBuoyantMeshData(float SubstepDeltaTime, FBodyInstance* BodyInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateBuoyantMeshData);
	{
		FBuoyantMeshData NewBuoyantMeshData;
		SCOPE_CYCLE_COUNTER(STAT_BuoyantMeshDataCreation);
		{
			BuoyancyData.SubFrameCircularBuffer[1].DeltaTime = SubstepDeltaTime;
			NewBuoyantMeshData = FBuoyantMeshData(BuoyancyData.MeshData.UniqueVertices, BuoyancyData.MeshData.Vertices, WaterGrid, BodyInstanceTransform, BodyInstance);
		}

		//Intersect the triangles and create the necessary sub-triangles
		for (int32 TriIndex = 0; TriIndex < NewBuoyantMeshData.Triangles.Num(); TriIndex++)
		{
			TArray<FBuoyantTriangle> CutSubmergedTris;
			TArray<FBuoyantVertex> WaterLineVerts;
			if (CutBuoyantTriangle(NewBuoyantMeshData.Triangles[TriIndex], CutSubmergedTris, WaterLineVerts, BodyInstance))
			{
				for (FBuoyantTriangle& SubmergedTriangle : CutSubmergedTris)
				{
					SubmergedTriangle.Depth = WaterGrid.GetDepthForPoint(SubmergedTriangle.Center);
					if (SubmergedTriangle.Depth < 0.0f) //Catch a case where the triangle's center could be above water, but its vertices aren't.
						NewBuoyantMeshData.Triangles[TriIndex].CutSubmergedArea += SubmergedTriangle.Area;
				}

				//This force has to applied before getting velocities for other forces
				CalculateAndApplyWaterEntryForce(TriIndex, NewBuoyantMeshData.Triangles[TriIndex], SubstepDeltaTime, BodyInstance);
				
				//Split each submerged triangle and apply their forces
				for (FBuoyantTriangle& SubmergedTriangle : CutSubmergedTris)
				{
					if (SubmergedTriangle.Depth < 0.0f)
					{
						NewBuoyantMeshData.SubmergedTriangles.Add(SubmergedTriangle);
						FBuoyantTriangle SplitTriangleOne, SplitTriangleTwo;
						SplitBuoyantTriangle(SubmergedTriangle, SplitTriangleOne, SplitTriangleTwo, BodyInstance);
						
						BuoyancyData.SubFrameCircularBuffer[1].CumulativeHydrostaticForces += SplitTriangleOne.HydrostaticForce + SplitTriangleTwo.HydrostaticForce;
						if (bDebugDrawBuoyantForce) //Doesn't need to use normals because only Z axis has force applied
						{
							UKismetSystemLibrary::DrawDebugArrow(GetWorld(), SplitTriangleOne.ForceCenter, SplitTriangleOne.ForceCenter + SplitTriangleOne.HydrostaticForce / ForceLengthScalar, 15.0f, FLinearColor::Blue, 0.0f, 1.0f);
							UKismetSystemLibrary::DrawDebugArrow(GetWorld(), SplitTriangleTwo.ForceCenter, SplitTriangleTwo.ForceCenter + SplitTriangleTwo.HydrostaticForce / ForceLengthScalar, 15.0f, FLinearColor::Blue, 0.0f, 1.0f);
						}

						if (!FMath::IsNearlyZero(SplitTriangleOne.HydrostaticForce.Size()))
							BodyInstance->AddForceAtPosition(SplitTriangleOne.HydrostaticForce, SplitTriangleOne.ForceCenter, false);

						if (!FMath::IsNearlyZero(SplitTriangleOne.HydrostaticForce.Size()))
							BodyInstance->AddForceAtPosition(SplitTriangleTwo.HydrostaticForce, SplitTriangleTwo.ForceCenter, false);

						ApplyDampingForcesForTriangle(SubmergedTriangle, SubstepDeltaTime, BodyInstance);
					}
				}
			}
		}

		BuoyancyData.SubFrameCircularBuffer[1].BuoyantData = NewBuoyantMeshData;
	}
}

bool UNetworkedBuoyantPawnMovementComponent::CutBuoyantTriangle(const FBuoyantTriangle& UnCutTriangle, TArray<FBuoyantTriangle>& CutSubmergedTriangles, TArray<FBuoyantVertex>& WaterLineVertices, FBodyInstance* BodyInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_CutPerTri);
	{
		const int32 VertL = 2;
		const int32 VertM = 1;
		const int32 VertH = 0;

		TArray<FBuoyantVertex> VertsByDepth = UnCutTriangle.GetVerticesByDepth();
		if (VertsByDepth[VertL].IsSubmerged())
		{
			const FVector MeshCenterLocation = BodyInstanceTransform.GetLocation();
			//Entire triangle is submerged
			if (VertsByDepth[VertH].IsSubmerged())
			{
				FVector TriCenter = (VertsByDepth[VertH].Vertex + VertsByDepth[VertM].Vertex + VertsByDepth[VertL].Vertex) / 3.0f;
				FBuoyantTriangle SubmergedTriangle = FBuoyantTriangle(VertsByDepth[VertH], VertsByDepth[VertM], VertsByDepth[VertL], WaterGrid.GetDepthForPoint(TriCenter), BodyInstance, MeshCenterLocation);
				CutSubmergedTriangles.Add(SubmergedTriangle);
				return true;
			}
			//Part of the triangle is submerged and part is surfaced
			else
			{
				float DepthL = VertsByDepth[VertL].Depth;
				float DepthM = VertsByDepth[VertM].Depth;
				float DepthH = VertsByDepth[VertH].Depth;
				FVector VertexL = VertsByDepth[VertL].Vertex;
				FVector VertexM = VertsByDepth[VertM].Vertex;
				FVector VertexH = VertsByDepth[VertH].Vertex;
				//Two submerged vertices - Two submerged triangles, one surfaced triangle
				if (VertsByDepth[VertM].IsSubmerged())
				{
					//New Buoyant Vertices are cut from the edges of the main triangle
					const FVector MH = VertexH - VertexM;
					const float TM = -DepthM / (DepthH - DepthM);
					FVector IM = (TM * MH) + VertexM;
					const FVector LH = VertexH - VertexL;
					const float TL = -DepthL / (DepthH - DepthL);
					FVector IL = (TL * LH) + VertexL;
					FBuoyantVertex VertexIM = FBuoyantVertex(IM, WaterGrid.GetDepthForPoint(IM));
					FBuoyantVertex VertexIL = FBuoyantVertex(IL, WaterGrid.GetDepthForPoint(IL));
					WaterLineVertices.Add(VertexIM);
					WaterLineVertices.Add(VertexIL);

					//Surfaced Sub-Triangle - (H, IL, IM)

					//Submerged Sub-Triangle One -  (IM, M, L)
					FVector TriCenterOne = (VertexIM.Vertex + VertsByDepth[VertM].Vertex + VertsByDepth[VertL].Vertex) / 3.0f;
					FBuoyantTriangle SubmergedTriangleOne = FBuoyantTriangle(VertexIM, VertsByDepth[VertM], VertsByDepth[VertL], WaterGrid.GetDepthForPoint(TriCenterOne), BodyInstance, MeshCenterLocation);
					CutSubmergedTriangles.Add(SubmergedTriangleOne);

					//Submerged Sub-Triangle Two - (IL, IM, L)
					FVector TriCenterTwo = (VertexIL.Vertex + VertexIM.Vertex + VertsByDepth[VertL].Vertex) / 3.0f;
					FBuoyantTriangle SubmergedTriangleTwo = FBuoyantTriangle(VertexIL, VertexIM, VertsByDepth[VertL], WaterGrid.GetDepthForPoint(TriCenterTwo), BodyInstance, MeshCenterLocation);
					CutSubmergedTriangles.Add(SubmergedTriangleTwo);
				}
				//One submerged vertex - One submerged triangle, two surfaced triangles
				else if (VertsByDepth[VertL].IsSubmerged())
				{
					//New Buoyant Vertices are cut from the edges of the main triangle
					const FVector ML = VertexM - VertexL;
					const float JMMult = -DepthL / (DepthM - DepthL);
					FVector JM = (JMMult * ML) + VertexL;
					const FVector HL = VertexH - VertexL;
					const float JLMult = -DepthL / (DepthH - DepthL);
					FVector JL = (JLMult * HL) + VertexL;
					FBuoyantVertex VertexJM = FBuoyantVertex(JM, WaterGrid.GetDepthForPoint(JM));
					FBuoyantVertex VertexJL = FBuoyantVertex(JL, WaterGrid.GetDepthForPoint(JL));
					WaterLineVertices.Add(VertexJM);
					WaterLineVertices.Add(VertexJL);
					
					//Submerged Sub-Triangle - (L, JL, JM)
					FVector TriCenter = (VertsByDepth[VertL].Vertex + VertexJL.Vertex + VertexJM.Vertex) / 3.0f;
					FBuoyantTriangle SubmergedTriangle = FBuoyantTriangle(VertsByDepth[VertL], VertexJL, VertexJM, WaterGrid.GetDepthForPoint(TriCenter), BodyInstance, MeshCenterLocation);
					CutSubmergedTriangles.Add(SubmergedTriangle);

					//Surfaced Sub-Triangle One - (H, M, JM)
					//Surfaced Sub-Triangle Two - (H, JL, JM)
				}

				return true;
			}
		}
	}

	return false;
}

void UNetworkedBuoyantPawnMovementComponent::SplitBuoyantTriangle(FBuoyantTriangle& UnSplitTriangle, FBuoyantTriangle& SplitTriangleUp, FBuoyantTriangle& SplitTriangleDown, FBodyInstance* BodyInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_SplitPerTri);
	{
		//This equation calls for vertices to be ordered by Height rather than Depth
		TArray<FBuoyantVertex> VertsByHeight = UnSplitTriangle.GetVerticesByHeight();
		const FVector MeshCenterLocation = BodyInstanceTransform.GetLocation();
		const int32 VertL = 2;
		const int32 VertM = 1;
		const int32 VertH = 0;
		const float DepthL = VertsByHeight[VertL].Depth;
		const float DepthM = VertsByHeight[VertM].Depth;
		const float DepthH = VertsByHeight[VertH].Depth;
		const FVector VertexL = VertsByHeight[VertL].Vertex;
		const FVector VertexM = VertsByHeight[VertM].Vertex;
		const FVector VertexH = VertsByHeight[VertH].Vertex;
		const FVector HL = VertexL - VertexH;
		float HeightHM = VertexH.Z - VertexM.Z;
		float HeightHL = VertexH.Z - VertexL.Z;
		FVector HorizontalCut = ((HL * HeightHM) / HeightHL) + VertexH;
		float BandCutHeight = WaterGrid.GetDepthForPoint(HorizontalCut);
		FBuoyantVertex FVertexL = FBuoyantVertex(VertexL, DepthL);
		FBuoyantVertex FVertexM = FBuoyantVertex(VertexM, DepthM);
		FBuoyantVertex FVertexH = FBuoyantVertex(VertexH, DepthH);
		FBuoyantVertex FVertexCut = FBuoyantVertex(HorizontalCut, BandCutHeight);
		FVector TriUpCenter = (FVertexH.Vertex + FVertexM.Vertex + FVertexCut.Vertex) / 3.0f;
		SplitTriangleUp = FBuoyantTriangle(FVertexH, FVertexM, FVertexCut, WaterGrid.GetDepthForPoint(TriUpCenter), true, BodyInstance, MeshCenterLocation);
		FVector TriDownCenter = (FVertexM.Vertex + FVertexCut.Vertex + FVertexL.Vertex) / 3.0f;
		SplitTriangleDown = FBuoyantTriangle(FVertexM, FVertexCut, FVertexL, WaterGrid.GetDepthForPoint(TriDownCenter), false, BodyInstance, MeshCenterLocation);
		//UPDATE_TASK: Refactor calc into struct, utilize multiple constructors. 
		SplitTriangleUp.HydrostaticForce = BuoyancyInformation.BuoyancyCoefficient * SplitTriangleUp.OutwardNormal * -SplitTriangleUp.Area * FMath::Abs(SplitTriangleUp.Depth) * -BuoyancyInformation.FluidDensity * GetGravityZ();
		SplitTriangleDown.HydrostaticForce = BuoyancyInformation.BuoyancyCoefficient * SplitTriangleDown.OutwardNormal * -SplitTriangleDown.Area * FMath::Abs(SplitTriangleDown.Depth) * -BuoyancyInformation.FluidDensity * GetGravityZ();
		UnSplitTriangle.HydrostaticForce = SplitTriangleUp.HydrostaticForce + SplitTriangleDown.HydrostaticForce;
	}
}

void UNetworkedBuoyantPawnMovementComponent::CalculateAndApplyWaterEntryForce(const int32 UncutTriangleIndex, FBuoyantTriangle& UnCutTriangle, float SubstepDeltaTime, FBodyInstance* BodyInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_WaterEntryForce);
	{
		/*
		* NOTE:
		* Substepping may be an issue here as the number of steps (60hz) may be too many to accurately capture violent impacts.
		* This is because substepping results in a smaller dt (1/60) therefore
		* it may be imperative to scale the capture of violent impacts
		* or utilize a different step rate for this and create a new buffer just for slamming - which in itself would create problems.
		* Alternatively we could dynamically adjust the threshold value (TriangleAccelerationMaximum) based on certain parameters. 
		*/
		if (BuoyancyData.SubFrameCircularBuffer.IsValidIndex(0) && BuoyancyData.SubFrameCircularBuffer[0].BuoyantData.Triangles.IsValidIndex(UncutTriangleIndex))
		{
			const float Theta = FMath::Clamp(UnCutTriangle.GetWaterDirection(), 0.0f, 1.0f);

			//Only add the slamming force if we're not receding from the water (Theta > zero)
			if (Theta > 0.0f)
			{
				if (bDebugDrawWaterEntryForce)
				{
					DrawDebugLine(GetWorld(), UnCutTriangle.Center, UnCutTriangle.Center + (100.0f * UnCutTriangle.Velocity.GetSafeNormal()), FColor::Green, false);
					DrawDebugLine(GetWorld(), UnCutTriangle.Center, UnCutTriangle.Center +  (100.0f * UnCutTriangle.OutwardNormal), FColor::Blue, false);
					DrawDebugSphere(GetWorld(), UnCutTriangle.Center, 16.0f, 4, FColor::Blue);
				}

				float LastSubmergedArea = BuoyancyData.SubFrameCircularBuffer[0].BuoyantData.Triangles[UncutTriangleIndex].CutSubmergedArea;
				float CurrentSubmergedArea = UnCutTriangle.CutSubmergedArea;
				FVector LastSweptWaterVolume = LastSubmergedArea * BuoyancyData.SubFrameCircularBuffer[0].BuoyantData.Triangles[UncutTriangleIndex].Velocity;
				FVector CurrentSweptWaterVolume = CurrentSubmergedArea * UnCutTriangle.Velocity;
				float TriangleAcceleration = FVector((CurrentSweptWaterVolume - LastSweptWaterVolume) / (UnCutTriangle.Area * SubstepDeltaTime)).Size();
				float TriangleAccelerationMaximum = (UnCutTriangle.Velocity.Size() * UnCutTriangle.Area) / (UnCutTriangle.Area * SubstepDeltaTime); //(UnCutTriangle.Velocity.Size() / SubstepDeltaTime);
				
				/*
				* Stopping Force Equation:
				* m * v * (2A * S)
				* m - Mass of the body instance
				* v - Velocity
				* A - Submerged Area of the Triangle
				* S - Total surface area of the mesh
				*/

				FVector StoppingForce = GetRootBodyInstanceMass() * UnCutTriangle.Velocity * ((2.0f * UnCutTriangle.CutSubmergedArea) / BuoyancyData.MeshData.TotalSurfaceArea);
				float ClampedAccelerationMagnitude = FMath::Clamp(TriangleAcceleration / TriangleAccelerationMaximum, 0.0f, 1.0f);
				//Dirty hack that resolves the issue of incorrectly trying to apply forces to triangles that are above the waterline & curve inwards when the upward velocity is positive
				const FVector PotentialWaterEntryForce = -FMath::Pow(ClampedAccelerationMagnitude, BuoyancyInformation.DampingForces.WaterEntryForcePower) * Theta * StoppingForce * BuoyancyInformation.DampingForces.WEFScalar;
				UnCutTriangle.WaterEntryForce = PotentialWaterEntryForce.Z >= 0.0f ? PotentialWaterEntryForce : FVector::ZeroVector;
				
				BuoyancyData.SubFrameCircularBuffer[1].CumulativeWaterEntryForces += UnCutTriangle.WaterEntryForce;

				if (!FMath::IsNearlyZero(UnCutTriangle.WaterEntryForce.Size()))
				{
					BodyInstance->AddForceAtPosition(UnCutTriangle.WaterEntryForce, UnCutTriangle.Center, false);

					if (bDebugDrawWaterEntryForce)
					{
						DrawDebugSphere(GetWorld(), UnCutTriangle.Center, 16.0f, 4, FColor::Red);
						DrawDebugLine(GetWorld(), UnCutTriangle.Center, UnCutTriangle.Center + UnCutTriangle.WaterEntryForce / ForceLengthScalar, FColor::Red, false);
					}
				}	
			}
		}
	}
}

float UNetworkedBuoyantPawnMovementComponent::GetRootBodyInstanceMass()
{
	ANetworkedBuoyantPawn* Owner = Cast<ANetworkedBuoyantPawn>(GetOwner());
	if (Owner)
	{
		FBodyInstance* BodyInstance = Owner->GetRootBodyInstance();
		if (BodyInstance)
			return BodyInstance->bOverrideMass == true ? BodyInstance->GetMassOverride() : BodyInstance->GetBodyMass();
	}
	
	return 0.0f;
}

void UNetworkedBuoyantPawnMovementComponent::ApplyDampingForcesForTriangle(FBuoyantTriangle& Triangle, float SubstepDeltaTime, FBodyInstance* BodyInstance)
{
	CalculateTrianglesForces(Triangle, SubstepDeltaTime, BodyInstance);

	SCOPE_CYCLE_COUNTER(STAT_ApplyForcesPerTri);
	{
		const FVector TriCenter = Triangle.Center;
		const FVector PressureDragForce = Triangle.PressureDragForce;
		const FVector WaterResistanceForce = Triangle.WaterResistanceForce;

		FVector CumulativeDampingForces = PressureDragForce + WaterResistanceForce;
		if (!FMath::IsNearlyZero(CumulativeDampingForces.Size()))
			BodyInstance->AddForceAtPosition(CumulativeDampingForces, TriCenter, false);
	}
}

void UNetworkedBuoyantPawnMovementComponent::CalculateTrianglesForces(FBuoyantTriangle& Triangle, float SubstepDeltaTime, FBodyInstance* BodyInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_CalcForcesPerTri);
	{
		if (!FMath::IsNearlyZero(Triangle.Velocity.Size()))
		{
			//Pressure Drag Force
			const float RefSpeed = BuoyancyInformation.DampingForces.bUseReferenceVelocity ? BuoyancyInformation.DampingForces.VelocityReferenceSpeed : Triangle.Velocity.Size();
			const float Theta = Triangle.GetWaterDirection();
			if (Theta >= 0.0f)
			{
				float LinearDragTerm = BuoyancyInformation.DampingForces.PressureDragLinearCoefficient * (Triangle.Velocity.Size() / RefSpeed);
				float QuadraticDragTerm = BuoyancyInformation.DampingForces.PressureDragQuadraticCoefficient * FMath::Pow(Triangle.Velocity.Size() / RefSpeed, 2);
				FVector PartialTerm = Triangle.Area * FMath::Pow(Theta, BuoyancyInformation.DampingForces.PressureDragFallOffPower) * Triangle.OutwardNormal;
				Triangle.PressureDragForce = BuoyancyInformation.DampingForces.PDFScalar * -(LinearDragTerm + QuadraticDragTerm) * PartialTerm;
				
				if (bDebugDrawPressureDragForce)
					UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Triangle.Center, Triangle.Center + Triangle.PressureDragForce / ForceLengthScalar, 15.0f, FColor::Orange, 0.0f, 1.0f);
				
				BuoyancyData.SubFrameCircularBuffer[1].CumulativePressureDragForces += Triangle.PressureDragForce;
			}
			else 
			{
				float LinearDragTerm = BuoyancyInformation.DampingForces.SuctionDragLinearCoefficient * (Triangle.Velocity.Size() / RefSpeed);
				float QuadraticDragTerm = BuoyancyInformation.DampingForces.SuctionDragQuadraticCoefficient * FMath::Pow(Triangle.Velocity.Size() / RefSpeed, 2);
				FVector PartialTerm = Triangle.Area * FMath::Pow(FMath::Abs(Theta), BuoyancyInformation.DampingForces.SuctionDragFallOffPower) * Triangle.OutwardNormal;
				Triangle.PressureDragForce = BuoyancyInformation.DampingForces.PDFScalar * (LinearDragTerm + QuadraticDragTerm) * PartialTerm;
				
				if (bDebugDrawPressureDragForce)
					UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Triangle.Center, Triangle.Center + Triangle.PressureDragForce / ForceLengthScalar, 15.0f, FColor::Yellow, 0.0f, 1.0f);

				BuoyancyData.SubFrameCircularBuffer[1].CumulativePressureDragForces += Triangle.PressureDragForce;
			}

			/*
			Viscous Water Resistance Force
			Fluid Dynamics Drag Equation:
			R = 1/2pCSV^2
			R = Magnitude of the resistance force
			p = Fluid density
			C = Resistance coefficient
			S = Surface Area
			V = Speed

			Reynolds Number (Rn):
			C (Resistance Coefficient) from the Fluid Dynamics Drag Equation can  be expressed utilizing:
			Rn = (VL)/v
			V = Speed of the body
			L = Length of the fluid across the body
			v = Viscosity of the fluid
			The Resistance coefficient is calculated as Cf(Rn) = 0.075/(log10Rn-2)^2
			*/

			const float Length = BuoyancyInformation.HullLength;
			const float ReynoldsNumber = (Triangle.Velocity.Size() * Length) / BuoyancyInformation.FluidViscosity;		
			const float ResistanceCoefficient = 0.075f / FMath::Pow(FMath::LogX(10, ReynoldsNumber) - 2.0f, 2.0f); 	//TODO: Benchmark cost of Log10 operation
			const FVector TangentialVelocity = FVector::CrossProduct(Triangle.OutwardNormal, FVector::CrossProduct(Triangle.OutwardNormal, Triangle.Velocity) / Triangle.Velocity.Size()) / Triangle.Velocity.Size();

			const FVector TangentialDirection = TangentialVelocity.GetSafeNormal(); 
			const FVector UnIntegratedTangentialFlow = Triangle.Velocity.Size() * TangentialDirection;
			Triangle.WaterResistanceForce = 0.5f * (BuoyancyInformation.FluidDensity) *  ResistanceCoefficient * Triangle.Area * UnIntegratedTangentialFlow * UnIntegratedTangentialFlow.Size() * BuoyancyInformation.DampingForces.VWRFScalar;
			
			BuoyancyData.SubFrameCircularBuffer[1].CumulativeWaterResistanceForce += Triangle.WaterResistanceForce;
			
			if(bDebugDrawViscousWaterResistanceForce)
				UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Triangle.Center, Triangle.Center + Triangle.WaterResistanceForce / ForceLengthScalar, 15.0f, FLinearColor::Green, 0.0f, 1.0f);
		}
	}
}

void UNetworkedBuoyantPawnMovementComponent::DrawBuoyantDebug()
{
	UWorld* World = GetWorld();
	FTransform Transform = BuoyantMesh->GetBodyInstance()->GetUnrealWorldTransform(); //TODO refactor pointer call

	if (bDebugDrawGrid)
	{
		for (int CRow = 0; CRow < WaterGrid.Cells.Num(); CRow++)
		{
			for (int CCol = 0; CCol < WaterGrid.Cells[CRow].Num(); CCol++)
			{
				for (int TriIndex = 0; TriIndex < 2; TriIndex++)
				{
					DrawDebugLine(World, WaterGrid.Cells[CRow][CCol].Triangles[TriIndex].Vertices[0]->Vertex, WaterGrid.Cells[CRow][CCol].Triangles[TriIndex].Vertices[1]->Vertex, FColor::White, false);
					DrawDebugLine(World, WaterGrid.Cells[CRow][CCol].Triangles[TriIndex].Vertices[1]->Vertex, WaterGrid.Cells[CRow][CCol].Triangles[TriIndex].Vertices[2]->Vertex, FColor::White, false);
					DrawDebugLine(World, WaterGrid.Cells[CRow][CCol].Triangles[TriIndex].Vertices[2]->Vertex, WaterGrid.Cells[CRow][CCol].Triangles[TriIndex].Vertices[0]->Vertex, FColor::White, false);
				}
			}
		}

		ANetworkedBuoyantPawn* Owner = Cast<ANetworkedBuoyantPawn>(GetOwner());
		if (Owner)
		{
			FBodyInstance* BodyInstance = Owner->GetRootBodyInstance();
			if (BodyInstance)
			{
				FBox BodyBoundsBox = BodyInstance->GetBodyBounds();
				DrawDebugBox(World, BodyInstance->GetUnrealWorldTransform().GetLocation(), BodyBoundsBox.GetExtent(), FColor::Yellow, false);
			}
		}
	}

	if (BuoyancyData.SubFrameCircularBuffer.IsValidIndex(1))
	{
		if (bShowForceTriangles)
		{
			for (FBuoyantTriangle& Triangle : BuoyancyData.SubFrameCircularBuffer[1].BuoyantData.SubmergedTriangles)
			{
				DrawDebugLine(World, Triangle.Vertices[0].Vertex, Triangle.Vertices[1].Vertex, FColor::Yellow, false, -1.0f, 000, 4.0f);
				DrawDebugLine(World, Triangle.Vertices[1].Vertex, Triangle.Vertices[2].Vertex, FColor::Yellow, false, -1.0f, 000, 4.0f);
				DrawDebugLine(World, Triangle.Vertices[2].Vertex, Triangle.Vertices[0].Vertex, FColor::Yellow, false, -1.0f, 000, 4.0f);
				DrawDebugSphere(World, Triangle.ForceCenter, 8.0f, 4, FColor::Yellow, false, -1.0f, 000, 4.0f);
				DrawDebugLine(World, Triangle.ForceCenter, Triangle.ForceCenter + ((Triangle.HydrostaticForce.Size() / ForceLengthScalar) * Triangle.OutwardNormal) / ForceLengthScalar, FColor::Yellow, false);
			}
		}

		if (bDebugDrawBuoyantForce)
		{
			if (BuoyancyData.SubFrameCircularBuffer.IsValidIndex(1))
			{
				FVector CenterOfMass = BuoyantMesh->BodyInstance.GetCOMPosition();
				DrawDebugSphere(World, CenterOfMass, 64.0f, 8, FColor::White, false);
			}
		}
		
		if (bDebugDrawHeading)
		{
			UKismetSystemLibrary::DrawDebugArrow(World, Transform.GetLocation(), Transform.GetLocation() + (1000.0f * BuoyantMesh->GetBodyInstance()->GetUnrealWorldAngularVelocityInRadians_AssumesLocked().GetSafeNormal()), 100.0f, FColor::Blue, 0.0f, 25.0f);
			UKismetSystemLibrary::DrawDebugArrow(World, Transform.GetLocation(), Transform.GetLocation() + (1000.0f * BuoyantMesh->GetBodyInstance()->GetUnrealWorldVelocity_AssumesLocked().GetSafeNormal()), 100.0f, FColor::Green, 0.0f, 25.0f);
		}
	}
	
	//UPDATE_TASK: Cleanup and add debug visuals
	if (bDebugDrawMeshData)
	{
		//Intersect the triangles and create the necessary sub-triangles
		if (BuoyancyData.SubFrameCircularBuffer.IsValidIndex(1))
		{
			for (int32 TriIndex = 0; TriIndex < BuoyancyData.SubFrameCircularBuffer[1].BuoyantData.Triangles.Num(); TriIndex++)
			{
				//Triangle Normal
				DrawDebugLine(GetWorld(), BuoyancyData.SubFrameCircularBuffer[1].BuoyantData.Triangles[TriIndex].Center, (500.0f *  BuoyancyData.SubFrameCircularBuffer[1].BuoyantData.Triangles[TriIndex].OutwardNormal) + BuoyancyData.SubFrameCircularBuffer[1].BuoyantData.Triangles[TriIndex].Center, FColor::White, false, -1.0f, 000, 8.0f);
				//Mesh Tris...
				//Mesh Verts...
			}
		}		
	}
}

void UNetworkedBuoyantPawnMovementComponent::PerformMovement(float SubstepDeltaTime, FBodyInstance* BodyInstance)
{
	//Add your custom movement code here, this is executed every sub-step.
}
