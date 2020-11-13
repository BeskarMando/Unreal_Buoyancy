/*=================================================
* FileName: NetworkedBuoyantPawnMovementComponent.h
*
* Created by: Tobias Moos
* Project name: Sails of War / OceanProject
* Unreal Engine version: 4.19
* Created on: 2020/01/08
*
* Last Edited on: 2020/7/24
* Last Edited by: Tobias Moos
*
* -------------------------------------------------
* Created for: Sails Of War - http://sailsofwargame.com/
* -------------------------------------------------
* The reading and storing of Mesh Data in CreateBuoyantData() is based off of an earlier system in the OceanProject created by: Justin Beales
* The approximated water grid surface & Triangle cutting is based off of an article written by: Jacques Kerner
* which can be found here: 
* https://www.gamasutra.com/view/news/237528/Water_interaction_model_for_boats_in_video_games.php
* The force calculations are based off of an article written by: Jacques Kerner
* which can be found here:
* https://www.gamasutra.com/view/news/263237/Water_interaction_model_for_boats_in_video_games_Part_2.php
* -------------------------------------------------
* For parts referencing UE4 code, the following copyright applies:
* Copyright 1998-2020 Epic Games, Inc. All Rights Reserved.
*
* Feel free to use this software in any commercial/free game.
* Selling this as a plugin/item, in whole or part, is not allowed.
* See "OceanProject\License.md" for full licensing details.
* =================================================*/
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Libraries/Buoyancy/BuoyancyLibrary.h"
#include "NetworkedBuoyantPawnMovementComponent.generated.h"

struct FBodyInstance;
DECLARE_STATS_GROUP(TEXT("NetworkedBuoyantPawnMovementComponent - Buoyancy Physics"), STATGROUP_BuoyancyPhysics, STATCAT_Advanced);
DECLARE_STATS_GROUP(TEXT("NetworkedBuoyantPawnMovementComponent - Movement Physics"), STATGROUP_PhysicsMovement, STATCAT_Advanced);

USTRUCT()
struct FBuoyancyInformationDampingForces
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float PressureDragLinearCoefficient = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float PressureDragQuadraticCoefficient = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float SuctionDragLinearCoefficient = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float SuctionDragQuadraticCoefficient = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		bool bUseReferenceVelocity = false;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float VelocityReferenceSpeed = 500.f;  //Centimeters per second

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float SuctionDragFallOffPower = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Drag")
		float PressureDragFallOffPower = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces")
		float WaterEntryForcePower = 2.0f; //A value of 1 makes the slamming force appear gradually, and the force is felt well below MaxTriAcceleration while a value greater than 2 only appears at the threshold of MaxTriAcceleration, in a stiff manner

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Scalars")
		FVector PDFScalar = FVector(1.f); //Scalar used to scale the Pressure and Suction Drag forces being applied

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Scalars")
		FVector WEFScalar = FVector(1.f); //Scalar used to scale the Water Entry forces being applied

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces|Scalars")
		FVector VWRFScalar = FVector(1.f); //Scalar used to scale the Viscous Water Resistance forces being applied

	FBuoyancyInformationDampingForces() {};

};

USTRUCT()
struct FPhysicsOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bOverrideMass = false;

	UPROPERTY(EditAnywhere, Category = "Physics", meta = (EditCondition = "bOverrideMass"))
		float MassOverride = 0.0f; //Mass Override in Kilograms

	UPROPERTY(EditAnywhere, Category = "Physics", meta = (EditCondition = "bAvancedSettingsEnabled"))
		FVector InertiaTensorScale = FVector(1.f);

	UPROPERTY(EditAnywhere, Category = "Physics", meta = (EditCondition = "bAvancedSettingsEnabled"))
		float AngularDamping = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Physics")
		FVector CenterOfMassNudgeOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Physics", meta = (EditCondition = "bAvancedSettingsEnabled"))
		float MaxAngularVelocity = 500.0f;

	FPhysicsOverrides() {};
};

USTRUCT()
struct FBuoyancyInformation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
		bool bAvancedSettingsEnabled = false;

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Fluid", meta = (EditCondition = "bAvancedSettingsEnabled"))
		float FluidDensity = 0.001027; //Scaled for centimeters (cm^3/Kg). To convert from Meters^3/Kilogram use:  (M^3/Kg x 10^-6)

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Fluid", meta = (EditCondition = "bAvancedSettingsEnabled"))
		float FluidViscosity = 0.00089f; //Scaled for centimeters (cm^2/Kg).

	UPROPERTY(EditAnywhere, Category = "Buoyancy|Forces")
		FVector BuoyancyCoefficient = FVector(0.0f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere)
		float WaterGridCellSize = 400.0f; //The size of each of the water grid's cells

	//UPDATE_TASK: REYNOLDS_NUMBER_LENGTH - Remove this variable
	UPROPERTY(EditAnywhere, Category = "Physics")
		float HullLength = 0.0f; //Used for Reynold's number calculations
		
	UPROPERTY(EditAnywhere, Category = "Physics")
		FBuoyancyInformationDampingForces DampingForces;

	UPROPERTY(EditAnywhere, Category = "Physics")
		FPhysicsOverrides PhysicsOverrides;

	FBuoyancyInformation() {};
};

UCLASS()
class SAILSOFWAR_API UNetworkedBuoyantPawnMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UNetworkedBuoyantPawnMovementComponent();
	
/** Buoyancy **/
public: 
	/**
	*	
	*	@return	bool -
	*/
	bool ShouldOverrideMass() { return BuoyancyInformation.PhysicsOverrides.bOverrideMass; }
	/**
	*	
	*	@return	float -
	*/
	float GetMassOverride() { return BuoyancyInformation.PhysicsOverrides.MassOverride; }

	/**
	*	Reads the BuoyantMeshComponent's triangle and vertex data to create our buoyancy information
	*	@param	NewBuoyantMesh - The new buoyant mesh component to use for buoyancy
	*	@param	OutWaterGrid - The new grid that's been created for buoyancy
	*	@param	OutBuoyancyData - The recreated information used for buoyancy
	*	@param	BodyInstance - The body instance of the buoyant mesh component
	*	@param	CellSize - The size of the cells for the water grid
	*	@param	OwnerLocation - The location of the buoyant mesh component
	*/
	void CreateBuoyantData(class UBuoyantMeshComponent* NewBuoyantMesh, FWaterGrid& OutWaterGrid, FBuoyancyData& OutBuoyancyData, FBodyInstance* BodyInstance, float CellSize, FVector OwnerLocation);

	/**
	*	@param	NewBuoyantMesh - The new buoyant mesh component to use for buoyancy
	*/
	void SetBuoyantMesh(class UBuoyantMeshComponent* NewBuoyantMesh);

	/**
	*	returns the size of the water grid's cells
	*	@return	float - the size of the cell
	*/
	float GetWaterGridCellSize() { return BuoyancyInformation.WaterGridCellSize; }

protected:
	UPROPERTY(EditAnywhere)
	FBuoyancyInformation BuoyancyInformation; //Adjustable values and settings for buoyancy

//TASK_UPDATE: REFACTOR_CLEANUP - cleanup debug checks
/** Debug **/
public:
	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawWaterline = false; //Setting this to true will cause the component to draw the waterline intersection around the mesh

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawForceTriangles = false; //Setting this to true will cause the component to draw the force triangles around the mesh

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawHeading = false; //Setting this to true will cause the component to draw the angular and linear velocities

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawMeshData = false; //Setting this to true will cause the component to draw the entire mesh's triangles and normals

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawGrid = true; //Setting this to true will cause the component to draw the angular and linear velocities

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawGridTargetBounds = true; //Setting this to true will cause the component to draw the body instance's bounding box

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawBuoyantForce = true; //Setting this to true will cause the component to draw the HF

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawCenterOfMass = true; //Setting this to true will cause the component to draw it's Center of Mass

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawPressureDragForce = true; //Setting this to true will cause the component to draw the PDF

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawWaterEntryForce = true; //Setting this to true will cause the component to draw the WEF

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawViscousWaterResistanceForce = true; //Setting this to true will cause the component to draw the VWRF

	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebugDrawCompareForces = true; //Setting this to true will cause the component to show the cumulative force applied for each force

	UPROPERTY(EditAnywhere, Category = "Debug")
		float ForceLengthScalar = 1000.0f; //Scalar used to divide forces by for visualizing forces

protected:
	/**
	*	Draws most of the debug visuals if that visual is enabled
	*/
	virtual void DrawBuoyantDebug();

	UPROPERTY()
		FBuoyancyData BuoyancyData; //Buoyancy Data used for physics simulation

	UPROPERTY()
		FWaterGrid WaterGrid; //Water Grid Data used for our water submersion simulation

protected:
	UPROPERTY()
		class UBuoyantMeshComponent* BuoyantMesh = nullptr; //Reference to the root component used for our physics simulation
	//IMPORT_TASK: Set this to your ocean actor - to do create height request function inside the movement class
	UPROPERTY()
		class ASOWOceanActor* OceanActor = nullptr; // Reference to ocean actor for height queries

/** Movement **/
public:
	virtual void PhysicsSubstep(float DeltaTime, FBodyInstance* BodyInstance);

protected:
	/**
	*	Override this function for any movement forces that need to be applied to the body
	*/
	virtual void PerformMovement(float SubstepDeltaTime, FBodyInstance* BodyInstance);


/** Physics **/
protected:
	UPROPERTY()
		FTransform BodyInstanceTransform = FTransform(); //Performance Optimization grab the transform at the start of the sub-frame
	
	/**
	*	Returns the owner's RootComponent's BodyInstance's mass
	*/
	virtual float GetRootBodyInstanceMass();

	//UPDATE_TASK: AIR_DRAG - Apply air drag forces here
	/**
	*	Calls SplitBuoyantTriangle() to split the triangle into two
	*	And applies the hydrostatic force, then calculates the other forces for the original unsplit triangle.
	*	@param	Triangle - The Triangle that we're applying forces to
	*	@param	DeltaSubstepTime - The delta time for the sub frame
	*	@param	BodyInstance - The FBodyInstance of a BuoyantMesh's static mesh to apply forces to
	*/
	virtual void ApplyDampingForcesForTriangle(FBuoyantTriangle& Triangle, float SubstepDeltaTime, FBodyInstance* BodyInstance);

	/**
	*	Compares the difference in area between this frame and the last using the BuoyancyData's SubFrameCircularBuffer and
	*	uses this to calculate and apply the Water Entry "Slamming" force for the triangle.
	*	@param	UnCutTriangleIndex - the index for the uncut triangle, used to see the previous frame's submersion for this triangle
	*	@param	UnCutTriangle - The triangle to cut into sub triangles
	*	@param	DeltaSubstepTime - The delta time for the sub frame
	*	@param	BodyInstance - The FBodyInstance of a BuoyantMesh's static mesh to apply forces to
	*/
	virtual void CalculateAndApplyWaterEntryForce(const int32 UncutTriangleIndex, FBuoyantTriangle& UnCutTriangle, float SubstepDeltaTime, FBodyInstance* BodyInstance);


	//UPDATE_TASK: AIR_DRAG - Calculate air drag on surfaced triangles
	//UPDATE_TASK: REYNOLDS_NUMBER_LENGTH - Actually calculate the length for Reynold's Number
	/**
	*	Calculates the Buoyant Force, and two of the damping forces - Water Resistance and Pressure Drag
	*	@param	DeltaSubstepTime - The delta time for the sub frame
	*	@param	BodyInstance - The FBodyInstance of a BuoyantMesh's static mesh to apply forces to
	*/
	virtual void CalculateTrianglesForces(FBuoyantTriangle& Triangle, float SubstepDeltaTime, FBodyInstance* BodyInstance);

private:
	/**
	*	Resizes, Re-Samples and moves the water grid to be aligned with the BodyInstances's new location
	*	@param	DeltaSubstepTime - The delta time for the sub frame
	*	@param	BodyInstance - The FBodyInstance of a BuoyantMesh's static mesh to find its location during the sub-frame
	*/
	void UpdateWaterGrid(float SubstepDeltaTime, FBodyInstance* BodyInstance);

	/**
	*	Iterate through the BuoyantMesh's triangles and vertices, transform them to world space
	*	Iterate through each triangle for submersion, and force calculations.
	*	@param	DeltaSubstepTime - The delta time for the sub frame
	*	@param	BodyInstance - The FBodyInstance of a BuoyantMesh's static mesh to find the it's location during the sub-frame
	*/
	void UpdateBuoyantMeshData(float SubstepDeltaTime, FBodyInstance* BodyInstance);

	//UPDATE_TASK: AIR_DRAG - Return surfaced triangles for air drag
	/**
	*	Checks submersion and cuts a Buoyant triangle into submerged and surfaced triangles
	*	@param	UnCutTriangle - The triangle to cut into sub triangles
	*	@param	CutSubmergedTriangles - An array of cut submerged triangles, can be 0, 1, 2 in length
	*	@param	WaterlineVertices - An array of the cut vertices that represent a waterline - useful for integration with VFX 
	*	@return returns true if the triangle is fully or partially submerged
	*/
	bool CutBuoyantTriangle(const FBuoyantTriangle& UnCutTriangle, TArray<FBuoyantTriangle>& CutSubmergedTriangles, TArray<FBuoyantVertex>& WaterLineVertices, FBodyInstance* BodyInstance);

	/**
	*	Splits a Buoyant triangle into two triangles sharing a horizontal edge
	*	@param	UnSplitTriangle - The triangle to split into two sub triangles
	*	@param	SplitTriangleUp - The first triangle, this triangle considers its horizontal edge to be up.
	*	@param	SplitTriangleDown - The second triangle, this triangle considers its horizontal edge to be down, therefore it is below the other split triangle
	*	@param	BodyInstance - The FBodyInstance of a BuoyantMesh's static mesh to find the it's location during the sub-frame
	*/
	void SplitBuoyantTriangle(FBuoyantTriangle& UnSplitTriangle, FBuoyantTriangle& SplitTriangleUp, FBuoyantTriangle& SplitTriangleDown, FBodyInstance* BodyInstance);

/*UMovementComponent Overrides*/
public:
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override; //Overriden to draw debug information every frame
};
