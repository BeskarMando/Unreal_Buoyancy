/*=================================================
* FileName: NetworkedBuoyantPawn.h
*
* Created by: Tobias Moos
* Project name: Sails of War
* Unreal Engine version: 4.19
* Created on: 2020/01/08
*
* Last Edited on: 2021/02/03
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
	*	Get the BuoyantMovementComponent
	*	@return UNetworkedBuoyantPawnMovementComponent* -  returns a const version  of the BuoyantMovementComponent
	*/
	class UNetworkedBuoyantPawnMovementComponent* GetBuoyantMovementComponent() const { return BuoyantMovementComponent; }

	/**
	*	Get the BuoyantMeshComponent
	*	@return UBuoyantMeshComponent* - returns a const version of the BuoyantMeshComponent
	*/
	class UBuoyantMeshComponent* GetBuoyantMeshComponent() const { return BuoyantMeshComponent; }

	/** The name of the buoyant movement component for the constructor */
	static FName BuoyantMovementComponentName;

	/** The name of the buoyant mesh for the constructor */
	static FName BuoyantMeshComponentName;

protected:
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

/** Networking **/
protected:
	/**
	*	Attempt to send an RPC update to the server if the interval value between updates has passed.
	*	@param	DeltaTime -  fractional time used to scale values by.
	*/
	virtual void ClientUpdateMovement(float DeltaTime);

	//TODO: This should be marked to be done after simulation of the movement for this frame!
	/**
	*	Update the buffer on the server and multi-cast this snapshot to the clients.
	*	@param	SnapShot - the movement snapshot received from the autonomous client
	*/
	UFUNCTION(Server, Unreliable, WithValidation)
	virtual void ServerRecieveMovement(const FMovementSnapshot& SnapShot);

	/**
	*	Handle the recieved snapshot from the client by adding it to the buffer
	*	@param	SnapShot - The movement snapshot received from the autonomous client 
	*/
	virtual void ServerHandleRecievedMovement(const FMovementSnapshot& SnapShot);

	/**
	*	Simulate the movement on the server 
	*	and update the server buffer after completion
	*	@param	DeltaTime - fractional time used to scale values by. 
	*/
	virtual void ServerSimulateMovement(float DeltaTime);

	/**
	*	Simulate the movement locally used for non-autonomous clients 
	*	and update the local buffer after completion
	*	@param	DeltaTime - fractional time used to scale values by.
	*/
	virtual void SimulateMovement(float DeltaTime);

	/**
	*	
	*	@param	SnapShot - 
	*/
	UFUNCTION(NetMulticast, Unreliable)
	virtual void MultiCastRecieveMovement(const FMovementSnapshot& SentSnapShot);
	
	UPROPERTY(EditDefaultsOnly)
		FPhysicsMovementReplication PhysicsReplicationData; //Physics Movement Replication Information and Data. Not replicated, but manually updated.

	//TODO: IMPLEMENT for further Debugging
	UFUNCTION(BlueprintCallable)
		void GetLocalSimulationMovementData(FPhysicsMovementReplication_Client& OutLocalData) {};
	UFUNCTION(BlueprintCallable)
		void GetServerSimulationMovementData(FPhysicsMovementReplication_Server& OutServerData) {};
	UFUNCTION(BlueprintCallable)
		void GetSnapshotBufferTargetIndex(float InTime, int32& OutTarget) {};
	UFUNCTION(BlueprintCallable)
		void GetSnapshotBufferCurrentIndex(float InTime, int32& OutTarget) {};
	UFUNCTION(BlueprintCallable)
		void GetSnapshotBufferTarget(FMovementSnapShotBuffer InBuffer, FMovementSnapshot& OutTargetSnapshot) {};
	UFUNCTION(BlueprintCallable)
		void GetSnapshotBufferCurrent(FMovementSnapShotBuffer InBuffer, FMovementSnapshot& OutCurrentSnapshot) {};
	UFUNCTION(BlueprintCallable)
		void GetMovedToSnapshot(FMovementSnapShotBuffer InBuffer, float InTime, FMovementSnapshot& OutSnapShot, float& OutAlpha) {};
	UFUNCTION(BlueprintImplementableEvent)
		void BPDrawDebug(float DeltaTime);

/*APawn Overrides*/
public:
	virtual void Restart() override; //Overridden to allow us to disable gravity calculations on the server for client pawns
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override; //Overridden to change the ReplicatedMovement Rep_Condition
	virtual void Tick(float DeltaTime) override; //Overridden to allow us to update our custom movement
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void PostInitializeComponents() override; //Overridden to set our required tick order & trigger the buoyant mesh setup
protected:
	UFUNCTION()
	virtual void OnRep_ReplicatedMovement() override; //Overriden for collision resolution - STILL TODO.
};
