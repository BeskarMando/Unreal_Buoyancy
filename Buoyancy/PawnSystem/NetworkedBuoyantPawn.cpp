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
#include "PhysicsMovementReplication.h"

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
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"

#define PrintWarning(Text) if(GEngine) GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Red, Text)
#define PrintMessage(Text) if(GEngine) GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Green, Text)
#define LogWarning(Text) UE_LOG(LogTemp, Warning, TEXT(Text))

FName ANetworkedBuoyantPawn::BuoyantMeshComponentName(TEXT("BuoyantMeshComponent"));
FName ANetworkedBuoyantPawn::BuoyantMovementComponentName(TEXT("BuoyantMovementComponent"));

ANetworkedBuoyantPawn::ANetworkedBuoyantPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BuoyantMeshComponent = CreateDefaultSubobject<UBuoyantMeshComponent>(ANetworkedBuoyantPawn::BuoyantMeshComponentName);
	BuoyantMeshComponent->SetHiddenInGame(true);
	BuoyantMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RootComponent = BuoyantMeshComponent;
	BuoyantMeshComponent->BodyInstance.bSimulatePhysics = true;
	//BuoyantMeshComponent->BodyInstance.SetEnableGravity(false);
	BuoyantMeshComponent->bAlwaysCreatePhysicsState = true;
	BuoyantMovementComponent = CreateDefaultSubobject<UNetworkedBuoyantPawnMovementComponent>(ANetworkedBuoyantPawn::BuoyantMovementComponentName);
	BuoyantMovementComponent->UpdatedComponent = BuoyantMeshComponent;
	OnCalculateCustomPhysics.BindUObject(BuoyantMovementComponent, &UNetworkedBuoyantPawnMovementComponent::PhysicsSubstep);
	
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	bReplicates = true;
	bReplicateMovement = false; //We don't utilize the built in movement replication system
	PhysicsReplicationData.AuthMovementReplication = FPhysicsMovementReplication_ClientAuth();
	PhysicsReplicationData.ServerMovementReplication = FPhysicsMovementReplication_Server();
	PhysicsReplicationData.LocalMovementReplication = FPhysicsMovementReplication_Client();
	PhysicsReplicationData.Settings = FPhysicsReplicationSettings();
}

void ANetworkedBuoyantPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CHANGE_CONDITION(ANetworkedBuoyantPawn, ReplicatedMovement, COND_SkipOwner);
}

void ANetworkedBuoyantPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	/*
	switch (Role)
	{
	case ROLE_Authority:
		DrawDebugSphere(GetWorld(), GetActorLocation(), 1000.0f, 6, FColor::Orange, false);
		break;

	case ROLE_AutonomousProxy:
		DrawDebugSphere(GetWorld(), GetActorLocation(), 1000.0f, 6, FColor::Yellow, false);
		break;

	case ROLE_SimulatedProxy:
		DrawDebugSphere(GetWorld(), GetActorLocation(), 1000.0f, 6, FColor::Red, false);
		break;
	default:
		break;
	}
	
	if (IsLocallyControlled())
		DrawDebugSphere(GetWorld(), GetActorLocation(), 750.0f, 6, FColor::White, false);

	switch (GetRemoteRole())
	{
	case ROLE_Authority:
		DrawDebugSphere(GetWorld(), GetActorLocation(), 500.0f, 6, FColor::Blue, false);
		break;

	case ROLE_AutonomousProxy:
		DrawDebugSphere(GetWorld(), GetActorLocation(), 500.0f, 6, FColor::Green, false);
		break;

	case ROLE_SimulatedProxy:
		DrawDebugSphere(GetWorld(), GetActorLocation(), 500.0f, 6, FColor::Purple, false);
		break;
	default:
		break;
	}
	*/
	if (IsLocallyControlled())
	{
		GetRootBodyInstance()->AddCustomPhysics(OnCalculateCustomPhysics);
		ClientUpdateMovement(DeltaTime);
	}
	else if(Role == ROLE_Authority)
		ServerSimulateMovement(DeltaTime);
	else if (Role == ROLE_SimulatedProxy)
		SimulateMovement(DeltaTime);
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
void ANetworkedBuoyantPawn::ClientUpdateMovement(float DeltaTime)
{
	if (IsLocallyControlled())
	{
		PhysicsReplicationData.AuthMovementReplication.TimeSinceLastPacketSent += DeltaTime;
		float SendRateFraction = 1.0f / PhysicsReplicationData.Settings.SendRate;
		if (PhysicsReplicationData.AuthMovementReplication.TimeSinceLastPacketSent >= SendRateFraction)
		{
			PhysicsReplicationData.AuthMovementReplication.TimeSinceLastPacketSent -= SendRateFraction;
			FBodyInstance* Body = GetRootBodyInstance();
			if (Body != nullptr)
			{
				FVector Loc = Body->GetUnrealWorldTransform_AssumesLocked().GetLocation();
				FQuat Rot = Body->GetUnrealWorldTransform_AssumesLocked().GetRotation();
				FVector LinVel = Body->GetUnrealWorldVelocity_AssumesLocked();
				FVector AngVel = FMath::RadiansToDegrees(Body->GetUnrealWorldAngularVelocityInRadians_AssumesLocked());
				float TimeStamp = GetWorld()->GetGameState<ASOWGameState>()->GetServerWorldTimeSeconds();
				FMovementSnapshot NewSnapShot = FMovementSnapshot(LinVel, AngVel, Loc, Rot, TimeStamp);
				ServerRecieveMovement(NewSnapShot);
				//Experiment and see if storing a copy of the snapshot for Player collision resolution is needed
			}
		}
	}
}

void ANetworkedBuoyantPawn::ServerRecieveMovement_Implementation(const FMovementSnapshot& SnapShot)
{
	ServerHandleRecievedMovement(SnapShot);
	MultiCastRecieveMovement(SnapShot);
}

//UPDATE_TASK: ADDITIONAL_NETWORKING - Implement cheating checks here
bool ANetworkedBuoyantPawn::ServerRecieveMovement_Validate(const FMovementSnapshot& SnapShot)
{
	return true;
}

void ANetworkedBuoyantPawn::ServerHandleRecievedMovement(const FMovementSnapshot& ClientSnapShot)
{
	if (!IsLocallyControlled() && Role == ROLE_Authority)
	{
		//UPDATE_TASK: Run anti-cheat and dynamic collision flags here
		PhysicsReplicationData.ServerMovementReplication.SnapshotBuffer.AddToBuffer(ClientSnapShot);
		PhysicsReplicationData.ServerMovementReplication.TimeSinceLastPacketRecieved = 0.0f;
	}
}

void ANetworkedBuoyantPawn::ServerSimulateMovement(float DeltaTime)
{
	if (!IsLocallyControlled() && Role == ROLE_Authority)
	{
		PhysicsReplicationData.ServerMovementReplication.TimeSinceLastPacketRecieved += DeltaTime;

		//IMPORT_TASK: Change to AGameState instead
		//UPDATE_TASK: Correctly update the server time to prevent drift by overriding GetServerWorldTimeSeconds()
		ASOWGameState* SOWGS = GetWorld()->GetGameState<ASOWGameState>();
		if (SOWGS)
		{
			float Time = GetWorld()->GetUnpausedTimeSeconds();

			const FMovementSnapShotBuffer& SnapShotBuffer = PhysicsReplicationData.ServerMovementReplication.SnapshotBuffer;		
			int32 TargetIndex;
			int32 CurrentIndex;
			if (SnapShotBuffer.HasElapsedMinTime(Time))
			{
				if (SnapShotBuffer.GetTargetSnapshotIndex(Time, TargetIndex) &&  SnapShotBuffer.GetCurrentSnapshotIndex(Time, CurrentIndex))
				{
					FBodyInstance* Body = GetRootBodyInstance();
					if (Body != nullptr)
					{
						const FMovementSnapshot& TargetSnapshot = SnapShotBuffer.Buffer[TargetIndex];
						const FMovementSnapshot& CurrentSnapshot = SnapShotBuffer.Buffer[CurrentIndex];

						/* TODO: IMPLEMENT SMOOTHING ALGORITHM? */
						/*
						Interpolation alpha formula for a buffer containing missing and or continuous snapshots is the following:
						(SnapshotInterval / (SnapshotB.Time - SnapshotA.Time)) * (CurrentTime - ((BufferSize * SnapshotInterval) + SnapshotA.Time))
						*/

						const float AT = CurrentSnapshot.TimeStamp;
						const float BT = TargetSnapshot.TimeStamp;
						const float CT = Time;
						const float BI = 1.0f / PhysicsReplicationData.Settings.SendRate;
						const float BS = PhysicsReplicationData.Settings.BufferSize / 1000.0f;
						const float BSI = BI * BS;
						const float IS = BI / (BT - AT);
						const float A = IS * (CT - (BSI + AT));

						const float Alpha = A;
						FQuat RotLerp = FQuat::Slerp(Body->GetUnrealWorldTransform_AssumesLocked().GetRotation(), TargetSnapshot.Rotation, Alpha); //FQuat::Identity;
						FVector LocLerp = UKismetMathLibrary::VLerp(Body->GetUnrealWorldTransform_AssumesLocked().GetLocation(), TargetSnapshot.Location,Alpha);//FVector::ZeroVector;
						FVector LinVelLerp = UKismetMathLibrary::VLerp(Body->GetUnrealWorldVelocity_AssumesLocked(), TargetSnapshot.LinearVelocity, Alpha);//FVector::ZeroVector;
						FVector AngVelLerp = UKismetMathLibrary::VLerp(Body->GetUnrealWorldAngularVelocityInRadians_AssumesLocked(), TargetSnapshot.AngularVelocity, Alpha);//FVector::ZeroVector;
						FTransform NewTransform = FTransform(RotLerp, LocLerp);

						Body->SetBodyTransform(NewTransform, ETeleportType::TeleportPhysics);
						Body->SetLinearVelocity(LinVelLerp, false);
						Body->SetAngularVelocityInRadians(AngVelLerp, false);
					}
				}
			}
			PhysicsReplicationData.ServerMovementReplication.SnapshotBuffer.Update(Time);
		}	
	}
}

void ANetworkedBuoyantPawn::SimulateMovement(float DeltaTime)
{
	//PhysicsReplicationData.LocalMovementReplication.TimeSinceLastPacketRecieved += DeltaTime;

	//IMPORT_TASK: Change to AGameState instead
	//UPDATE_TASK: Correctly update the server time to prevent drift by overriding GetServerWorldTimeSeconds()
	ASOWGameState* SOWGS = GetWorld()->GetGameState<ASOWGameState>();
	if (SOWGS)
	{
		float Time = SOWGS->GetServerWorldTimeSeconds();

		const FMovementSnapShotBuffer& SnapShotBuffer = PhysicsReplicationData.LocalMovementReplication.SnapshotBuffer;
		int32 TargetIndex;
		int32 CurrentIndex;
		if (SnapShotBuffer.HasElapsedMinTime(Time))
		{
			if (SnapShotBuffer.GetTargetSnapshotIndex(Time, TargetIndex) && SnapShotBuffer.GetCurrentSnapshotIndex(Time, CurrentIndex))
			{
				FBodyInstance* Body = GetRootBodyInstance();
				if (Body != nullptr)
				{
					const FMovementSnapshot& TargetSnapshot = SnapShotBuffer.Buffer[TargetIndex];
					const FMovementSnapshot& CurrentSnapshot = SnapShotBuffer.Buffer[CurrentIndex];
					/* TODO: IMPLEMENT SMOOTHING ALGORITHM? */
					/*
					Interpolation alpha formula for a buffer containing missing and or continuous snapshots is the following:
					(SnapshotInterval / (SnapshotB.Time - SnapshotA.Time)) * (CurrentTime - ((BufferSize * SnapshotInterval) + SnapshotA.Time))
					*/

					const float AT = CurrentSnapshot.TimeStamp;
					const float BT = TargetSnapshot.TimeStamp;
					const float CT = Time;
					const float BI = 1.0f / PhysicsReplicationData.Settings.SendRate;
					const float BS = PhysicsReplicationData.Settings.BufferSize / 1000.0f;
					const float BSI = BI * BS;
					const float IS = BI / (BT - AT);
					const float A = IS * (CT - (BSI + AT));

					const float Alpha = A;
					FQuat RotLerp = FQuat::Slerp(Body->GetUnrealWorldTransform_AssumesLocked().GetRotation(), TargetSnapshot.Rotation, Alpha); //FQuat::Identity;
					FVector LocLerp = UKismetMathLibrary::VLerp(Body->GetUnrealWorldTransform_AssumesLocked().GetLocation(), TargetSnapshot.Location, Alpha);//FVector::ZeroVector;
					FVector LinVelLerp = UKismetMathLibrary::VLerp(Body->GetUnrealWorldVelocity_AssumesLocked(), TargetSnapshot.LinearVelocity, Alpha);//FVector::ZeroVector;
					FVector AngVelLerp = UKismetMathLibrary::VLerp(Body->GetUnrealWorldAngularVelocityInRadians_AssumesLocked(), TargetSnapshot.AngularVelocity, Alpha);//FVector::ZeroVector;
					FTransform NewTransform = FTransform(RotLerp, LocLerp);

					Body->SetBodyTransform(NewTransform, ETeleportType::TeleportPhysics);
					Body->SetLinearVelocity(LinVelLerp, false);
					Body->SetAngularVelocityInRadians(AngVelLerp, false);
				}
			}
		}
		PhysicsReplicationData.LocalMovementReplication.SnapshotBuffer.Update(Time);
	}
}

void ANetworkedBuoyantPawn::MultiCastRecieveMovement_Implementation(const FMovementSnapshot& SentSnapShot)
{
	if (Role == ROLE_SimulatedProxy) //Proxy Client
	{
		PhysicsReplicationData.LocalMovementReplication.SnapshotBuffer.AddToBuffer(SentSnapShot);
	}
}

//UPDATE_TASK: ADDITIONAL_NETWORKING - Handle Player on Player Collision here
void ANetworkedBuoyantPawn::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();
}

void ANetworkedBuoyantPawn::Restart()
{
	Super::Restart();
	if (GetNetMode() < NM_Client && !IsLocallyControlled())
	{
		FBodyInstance* Body = GetRootBodyInstance();
		if (Body != nullptr)
		{
			Body->SetEnableGravity(false);
		}
	}
}

void ANetworkedBuoyantPawn::BeginPlay()
{
	Super::BeginPlay();
}
