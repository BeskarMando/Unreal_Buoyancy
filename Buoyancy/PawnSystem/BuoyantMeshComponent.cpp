/*=================================================
* FileName: BuoyantMeshComponent.h
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

#include "BuoyantMeshComponent.h"

UBuoyantMeshComponent::UBuoyantMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetComponentTickEnabled(false);
	bAutoActivate = true;
	bWantsInitializeComponent = true;
	SetSimulatePhysics(true);
	BodyInstance.AngularDamping = 0.0f;
	BodyInstance.LinearDamping = 0.0f;
	SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	SetCollisionObjectType(ECC_Pawn);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

}