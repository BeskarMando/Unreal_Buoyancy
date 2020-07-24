/*=================================================
* FileName: NetworkedBuoyantPawn.h
*
* Created by: Tobias Moos
* Project name: Sails of War / OceanProject
* Unreal Engine version: 4.19
* Created on: 2020/01/08
*
* Last Edited on: 2020/03/6
* Last Edited by: Tobias Moos
* -------------------------------------------------
* Useful Resources on Networking Physics Movement:
* -------------------------------------------------
* https://gafferongames.com/categories/networked-physics/
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
#pragma once

//Project Includes:
#include "NetworkedBuoyantPawnMovementComponent.h"

//Engine Includes:
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "UObject/CoreNet.h"
#include "Engine/NetSerialization.h"
#include "Engine/EngineTypes.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

#include "NetworkedBuoyantPawn.generated.h"

class UBuoyantMeshComponent;
struct FBodyInstance;

USTRUCT()
struct FSnapshotSettings
{
	GENERATED_BODY()

	UPROPERTY()
		uint32 SendRate = 30; //Number of snapshots to send per second

	UPROPERTY()
		float BufferDelay = 100.0f; //in milliseconds

	FSnapshotSettings() {};
};

USTRUCT()
struct FMovementSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
		FVector LinearVelocity;

	UPROPERTY()
		FVector AngularVelocity;

	UPROPERTY()
		FVector Location;

	UPROPERTY()
		FRotator Rotation;

	UPROPERTY()
		float ClientTimeStamp = 0.0f;

	FMovementSnapshot() {};
	FMovementSnapshot(FVector LinVel, FVector AngVel, FVector Loc, FRotator Rot, float Timestamp) :
	LinearVelocity(LinVel), AngularVelocity(AngularVelocity), Location(Loc), Rotation(Rot), ClientTimeStamp(Timestamp){}
};

USTRUCT()
struct FSnapShotBuffer
{
	GENERATED_BODY()

	UPROPERTY()
		TArray<FMovementSnapshot> SnapshotBuffer;

	UPROPERTY()
		float Delay = 100.0f;

	UPROPERTY()
		float TimeSinceLastBufferUpdate = 0.0f;

	FSnapShotBuffer() {};
	FSnapShotBuffer(uint32 Size, float BufferDelay)
	{
		SnapshotBuffer.SetNum(Size);
		Delay = BufferDelay;
	}

	void UpdateSnapShots(FMovementSnapshot NewSnapshot)
	{
		SnapshotBuffer.Empty();
		SnapshotBuffer.Add(NewSnapshot);
	}

	void UseSnapShot(int32 Index)
	{
		SnapshotBuffer.RemoveAt(Index);
	}
};

UCLASS()
class SAILSOFWAR_API ANetworkedBuoyantPawn : public APawn
{
	GENERATED_BODY()

/** Setup & Components **/
public:
	ANetworkedBuoyantPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	/**
	*	
	*	@return	class UNetworkedBuoyantPawnMovementComponent* -
	*/
	class UNetworkedBuoyantPawnMovementComponent* GetBuoyantMovementComponent() const { return BuoyantMovementComponent; }

	/**
	*	
	*	@return	class UBuoyantMeshComponent* -
	*/
	class UBuoyantMeshComponent* GetBuoyantMeshComponent() const { return BuoyantMeshComponent; }

	/**  */
	static FName BuoyantMovementComponentName;

	/**  */
	static FName BuoyantMeshComponentName;

private:
	UPROPERTY(EditDefaultsOnly)
		class UNetworkedBuoyantPawnMovementComponent* BuoyantMovementComponent; //Movement Component to handle buoyancy
	
	UPROPERTY(EditDefaultsOnly)
		class UBuoyantMeshComponent* BuoyantMeshComponent; //The buoyant mesh component for our pawn

/** Physics **/
public:

	/**
	*	Finds and returns the root body instance of our BuoyantMeshComponent
	*	@return	FBodyInstance* - the body instance to return
	*/
	FBodyInstance* GetRootBodyInstance();
	FCalculateCustomPhysics OnCalculateCustomPhysics;

public:
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void PostInitializeComponents() override; //Overridden to set our required tick order & trigger the buoyant mesh setup

/** Networking **/
protected:
	/**
	*	
	*/
	virtual void ClientPackAndSendSnapshot();

	/**
	*
	*	@param	SnapShot -
	*/
	UFUNCTION(Server, Unreliable, WithValidation)
	virtual void ServerReceiveSnapShot(FMovementSnapshot SnapShot);

	/**
	*	
	*	@param	SnapShot - 
	*/
	virtual void ServerUpdateSnapshotBuffer(FMovementSnapshot SnapShot);

	UPROPERTY(ReplicatedUsing = OnRep_SnapShotReplicatedMovement)
		FSnapShotBuffer ServerSnapShotBuffer; //The server's snapshot buffer used for collision resolution and keeping track of the snapshots

	UPROPERTY(/*NotReplicated*/)
		FSnapShotBuffer LocalSnapShotBuffer; //The local snapshot buffer for Simulated Proxies

	UFUNCTION()
	/**
	*	
	*/
	virtual void OnRep_SnapShotReplicatedMovement();

	/**
	*	
	*/
	virtual void PerformBufferSnapshotMovement();
};
