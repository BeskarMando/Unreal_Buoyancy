/*=================================================
* FileName: NetworkedBuoyantPawn.h
*
* Created by: Tobias Moos
* Project name: Sails of War / OceanProject
* Unreal Engine version: 4.19
* Created on: 2020/01/08
*
* Last Edited on: 2020/07/24
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
#include "NetworkedBuoyantPawn.h"
#include "BuoyantMeshComponent.h"
#include "NetworkedBuoyantPawnMovementComponent.h"

//IMPORT_TASK: Change to Engine variants
//UPDATE_TASK: Provide the ability to override them in the constructor with custom classes 
//Project Includes:
#include "SailsOfWar/Public/SOWSpringArmComponent.h"
#include "SailsOfWar/Public/SOWCameraComponent.h"
#include "SailsOfWar/Public/SOWGameplayStatics.h"
#include "SailsOfWar/Public/SOWGameState.h"

//Engine Includes: 
#include "DisplayDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "UnrealNetwork.h"

#define PrintWarning(Text) if(GEngine) GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Red, Text)
#define PrintMessage(Text) if(GEngine) GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Green, Text)
#define LogWarning(Text) UE_LOG(LogTemp, Warning, TEXT(Text))


FName ANetworkedBuoyantPawn::BuoyantMeshComponentName(TEXT("BuoyantMeshComponent"));
FName ANetworkedBuoyantPawn::BuoyantMovementComponentName(TEXT("BuoyantMovementComponent"));

ANetworkedBuoyantPawn::ANetworkedBuoyantPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BuoyantMeshComponent = CreateDefaultSubobject<UBuoyantMeshComponent>(ANetworkedBuoyantPawn::BuoyantMeshComponentName);
	BuoyantMeshComponent->bAlwaysCreatePhysicsState = true;

	RootComponent = BuoyantMeshComponent;	
	BuoyantMovementComponent = CreateDefaultSubobject<UNetworkedBuoyantPawnMovementComponent>(ANetworkedBuoyantPawn::BuoyantMovementComponentName);
	OnCalculateCustomPhysics.BindUObject(BuoyantMovementComponent, &UNetworkedBuoyantPawnMovementComponent::PhysicsSubstep);
	BuoyantMovementComponent->UpdatedComponent = BuoyantMeshComponent;
	BuoyantMeshComponent->SetHiddenInGame(true);
	BuoyantMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicateMovement = false; //We use our own custom MovementReplication for Buffer support.
}

void ANetworkedBuoyantPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ANetworkedBuoyantPawn, ServerSnapShotBuffer, COND_SkipOwner); 
}

void ANetworkedBuoyantPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//Only execute if the pawn is the auth. client - supports listen servers
	if (Role == ROLE_AutonomousProxy || (Role == ROLE_Authority && IsLocallyControlled()))
	{
		GetRootBodyInstance()->AddCustomPhysics(OnCalculateCustomPhysics);
		ClientPackAndSendSnapshot();
	}
	else
	{
		PerformBufferSnapshotMovement();
	}
}

FBodyInstance* ANetworkedBuoyantPawn::GetRootBodyInstance()
{
	if (BuoyantMeshComponent != nullptr)
		return BuoyantMeshComponent->GetBodyInstance();

	return nullptr;
}

UPawnMovementComponent* ANetworkedBuoyantPawn::GetMovementComponent() const
{
	return BuoyantMovementComponent;
}

void ANetworkedBuoyantPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!IsPendingKill())
	{
		if (BuoyantMovementComponent && BuoyantMeshComponent)
		{
			if (BuoyantMovementComponent->PrimaryComponentTick.bCanEverTick)
				BuoyantMeshComponent->PrimaryComponentTick.AddPrerequisite(BuoyantMovementComponent, BuoyantMovementComponent->PrimaryComponentTick);

			if (BuoyantMovementComponent->ShouldOverrideMass() && BuoyantMeshComponent->GetBodyInstance() != nullptr)
				BuoyantMeshComponent->SetMassOverrideInKg(NAME_None, BuoyantMovementComponent->GetMassOverride());

			BuoyantMovementComponent->SetBuoyantMesh(BuoyantMeshComponent);
		}
	}
}


/** Networking **/
void ANetworkedBuoyantPawn::ClientPackAndSendSnapshot()
{
	if (Role == ROLE_AutonomousProxy || (Role == ROLE_Authority && IsLocallyControlled()))
	{
		FBodyInstance* Body = GetRootBodyInstance();
		if (Body != nullptr)
		{
			FVector Loc = Body->GetUnrealWorldTransform_AssumesLocked().GetLocation();
			FRotator Rot = Body->GetUnrealWorldTransform_AssumesLocked().GetRotation().Rotator();
			FVector LinVel = Body->GetUnrealWorldVelocity_AssumesLocked();
			FVector AngVel = FMath::RadiansToDegrees(Body->GetUnrealWorldAngularVelocityInRadians_AssumesLocked());
			float TimeStamp = GetWorld()->GetGameState<ASOWGameState>()->GetServerWorldTimeSeconds();
			FMovementSnapshot NewSnapShot = FMovementSnapshot(LinVel, AngVel, Loc, Rot, TimeStamp);
			ServerReceiveSnapShot(NewSnapShot);
		}
	}
}

void ANetworkedBuoyantPawn::ServerReceiveSnapShot_Implementation(FMovementSnapshot SnapShot)
{
	//Verify Snapshot
	//Update snapshot buffer
	ServerUpdateSnapshotBuffer(SnapShot);
}

bool ANetworkedBuoyantPawn::ServerReceiveSnapShot_Validate(FMovementSnapshot SnapShot)
{
	return true;
}

void ANetworkedBuoyantPawn::ServerUpdateSnapshotBuffer(FMovementSnapshot SnapShot)
{
	ServerSnapShotBuffer.UpdateSnapShots(SnapShot); //Triggers an OnRep Event
}

void ANetworkedBuoyantPawn::OnRep_SnapShotReplicatedMovement()
{
	LocalSnapShotBuffer = ServerSnapShotBuffer;
	//Update the local snapshot buffer
}

void ANetworkedBuoyantPawn::PerformBufferSnapshotMovement()
{
	FBodyInstance* RootBodyInstance = GetRootBodyInstance();
	if (RootBodyInstance)
	{
		if (Role == ROLE_Authority && !IsLocallyControlled())
		{
			if (ServerSnapShotBuffer.SnapshotBuffer.IsValidIndex(0))
			{
				FTransform NewTransform = FTransform(ServerSnapShotBuffer.SnapshotBuffer[0].Rotation, ServerSnapShotBuffer.SnapshotBuffer[0].Location);
				RootBodyInstance->SetBodyTransform(NewTransform, ETeleportType::None);
				RootBodyInstance->SetLinearVelocity(ServerSnapShotBuffer.SnapshotBuffer[0].LinearVelocity, false);
				RootBodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(ServerSnapShotBuffer.SnapshotBuffer[0].AngularVelocity), false);
			}
		}
		else if(Role == ROLE_SimulatedProxy)
		{
			if (LocalSnapShotBuffer.SnapshotBuffer.IsValidIndex(0))
			{
				FTransform NewTransform = FTransform(LocalSnapShotBuffer.SnapshotBuffer[0].Rotation, LocalSnapShotBuffer.SnapshotBuffer[0].Location);
				RootBodyInstance->SetBodyTransform(NewTransform, ETeleportType::None);
				RootBodyInstance->SetLinearVelocity(LocalSnapShotBuffer.SnapshotBuffer[0].LinearVelocity, false);
				RootBodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(LocalSnapShotBuffer.SnapshotBuffer[0].AngularVelocity), false);
			}
		}
	}
}
