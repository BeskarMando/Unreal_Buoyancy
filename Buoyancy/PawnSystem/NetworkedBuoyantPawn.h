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
#include "PhysicsMovementReplication.h"

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

UCLASS()
class SAILSOFWAR_API ANetworkedBuoyantPawn : public APawn
{
	GENERATED_BODY()

/** Setup & Components **/
public:
	ANetworkedBuoyantPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	/**
	*	
	*	@return UNetworkedBuoyantPawnMovementComponent* -
	*/
	class UNetworkedBuoyantPawnMovementComponent* GetBuoyantMovementComponent() const { return BuoyantMovementComponent; }

	/**
	*	
	*	@return UBuoyantMeshComponent* -
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

	FCalculateCustomPhysics OnCalculateCustomPhysics; //Binds the pawn tick to our sub-steps in the MovementComponent

public:
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void PostInitializeComponents() override; //Overridden to set our required tick order & trigger the buoyant mesh setup


/** Networking **/
protected:
	/**
	*	Attempt to send an RPC update to the server if the interval between updates has passed.
	*	@param	DeltaTime - 
	*/
	virtual void ClientUpdateMovement(float DeltaTime);

	/**
	*
	*	@param	SnapShot -
	*/
	UFUNCTION(Server, Unreliable, WithValidation)
	virtual void ServerRecieveMovement(const FMovementSnapshot& SnapShot);

	/**
	*	
	*	@param	SnapShot - 
	*/
	virtual void ServerHandleRecievedMovement(const FMovementSnapshot& SnapShot);

	/**
	*	
	*	@param	DeltaTime - 
	*/
	virtual void ServerSimulateMovement(float DeltaTime);

	/**
	*	
	*	@param	DeltaTime - 
	*/
	virtual void SimulateMovement(float DeltaTime);

	/**
	*	
	*	@param	SnapShot - 
	*/
	UFUNCTION(NetMulticast, Unreliable)
	virtual void MultiCastRecieveMovement(const FMovementSnapshot& SentSnapShot);
	
	UPROPERTY()
		FPhysicsMovementReplication PhysicsReplicationData; //Physics Movement Replication Information and Data. Not replicated, but manually updated.

	


/*APawn Overrides*/
public:
	virtual void Restart() override;	//Overridden to allow us to disable gravity calculations on the server for client pawns
protected:
	virtual void BeginPlay() override;	//...
	UFUNCTION()
	virtual void OnRep_ReplicatedMovement() override;	//...


};