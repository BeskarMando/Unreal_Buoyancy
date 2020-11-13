
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
#pragma once

//Engine Includes: 
#include "Interfaces/NetworkPredictionInterface.h"
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"

#include "PhysicsMovementReplication.generated.h"

UENUM()
enum EEventFlag
{
	EF_None,
	EF_Collision,
	EF_Correction,
};

USTRUCT()
struct FPhysicsReplicationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
		uint32 SendRate = 20; //Number of snapshots to send per second

	UPROPERTY(EditAnywhere)
		float BufferSize = 500.0f; //in milliseconds

		FPhysicsReplicationSettings() {};
};
//TODO: COMPRESS USING QUANTIZED VECTORS
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
		FQuat Rotation;

	UPROPERTY()
		float TimeStamp = 0.0f;

	UPROPERTY()
		int8 EventFlag = 0;

	FMovementSnapshot() {};
	FMovementSnapshot(FVector LinVel, FVector AngVel, FVector Loc, FQuat Rot, float Time) :
		LinearVelocity(LinVel), AngularVelocity(AngularVelocity), Location(Loc), Rotation(Rot), TimeStamp(Time) {}
};

USTRUCT()
struct FMovementSnapShotBuffer
{
	GENERATED_BODY()

	UPROPERTY()
		TArray<FMovementSnapshot> Buffer;

	UPROPERTY()
		float BufferDelay = 500.0f; //Delay in milliseconds 

	UPROPERTY()
		float BufferInterval = 50.0f;


	FMovementSnapShotBuffer() {};

	

	//TODO: CLEANUP
	void Update(float Time)
	{
		
		const float BufferedTime = GetBufferedTime(Time);
		const float Min = FMath::RoundToZero(BufferedTime / GetBufferInterval()) * GetBufferInterval(); //Always keep the previous timestamp!

		TArray<int32>IndicesToKeep;
		TArray<FMovementSnapshot> NewBuffer;
		for (int i = 0; i < Buffer.Num(); i++)
		{
			if (Buffer[i].TimeStamp >= Min)
				IndicesToKeep.Add(i);
		}
		for (int i = 0; i < IndicesToKeep.Num(); i++)
		{
			NewBuffer.Add(Buffer[IndicesToKeep[i]]);
		}

		Buffer = NewBuffer;
		
		Buffer.Sort(TimeStampPredicate);
		
	}
	
	void AddToBuffer(const FMovementSnapshot& SnapShot)
	{
		Buffer.Add(SnapShot);
	}

	void RemoveFromBuffer(int32 Index)
	{
		if (Buffer.IsValidIndex(Index))
			Buffer.RemoveAt(Index);
	}

	bool GetTargetSnapshotIndex(float Time, int32& Index) const
	{
		if (HasElapsedMinTime(Time))
		{
			const float BufferedTime = GetBufferedTime(Time);
			for (int32 i = 0; i < Buffer.Num(); i++)
			{
				const FMovementSnapshot& Snapshot = Buffer[i];
				if (Snapshot.TimeStamp > BufferedTime)
				{
					Index = i;
					return true;
				}
			}

			return false;
		}

		return false;
	}

	bool GetCurrentSnapshotIndex(float Time, int32& Index) const
	{
		if (HasElapsedMinTime(Time))
		{
			const float BufferedTime = GetBufferedTime(Time);
			for (int i = 0; i < Buffer.Num(); i++)
			{
				const FMovementSnapshot& Snapshot = Buffer[i];
				if (Snapshot.TimeStamp < BufferedTime)
				{
					Index = i;
					return true;
				}
			}

			return false;
		}

		return false;
	}
	
	float GetBufferedTime(float Time) const { return Time - (BufferDelay / 1000.0f); } 
	float GetBufferInterval() const { return BufferInterval / 1000.0f; }
	float HasElapsedMinTime(float Time) const { return GetBufferedTime(Time) >= 0.0f; }

	inline static bool TimeStampPredicate(const FMovementSnapshot& A, const FMovementSnapshot& B)
	{
		return (A.TimeStamp < B.TimeStamp);
	}
	

	
/*
* Current Body Info -
* Previous Snapshot - 
* Target Snapshot - 
*/
};

USTRUCT()
struct  FPhysicsMovementReplication_ClientAuth
{
	GENERATED_BODY()

	UPROPERTY(NotReplicated)
		float TimeSinceLastPacketSent = 0.0f;

	UPROPERTY(NotReplicated)
		FMovementSnapShotBuffer SnapshotBuffer;
		
	FPhysicsMovementReplication_ClientAuth() {};

};

USTRUCT()
struct FPhysicsMovementReplication_Client
{
	GENERATED_BODY()

	UPROPERTY(NotReplicated)
		float TimeOffset = 0.0f;

	UPROPERTY(NotReplicated)
		FMovementSnapShotBuffer SnapshotBuffer;

	FPhysicsMovementReplication_Client() {};
};

USTRUCT()
struct FPhysicsMovementReplication_Server
{
	GENERATED_BODY()

	UPROPERTY(NotReplicated)
		float TimeSinceLastPacketRecieved = 0.0f;

	UPROPERTY(NotReplicated)
		FMovementSnapShotBuffer SnapshotBuffer;

	FPhysicsMovementReplication_Server() {};
};

USTRUCT()
struct FPhysicsMovementReplication
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
		FPhysicsReplicationSettings Settings;

	UPROPERTY()
		FPhysicsMovementReplication_Client LocalMovementReplication; //The local snapshot buffer for Simulated Proxies

	UPROPERTY()
		FPhysicsMovementReplication_Server ServerMovementReplication; //The server's snapshot buffer used for collision resolution and keeping track of the snapshots

	UPROPERTY()
		FPhysicsMovementReplication_ClientAuth AuthMovementReplication;

	FPhysicsMovementReplication() {};
};