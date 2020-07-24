// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BuoyancyLibrary.generated.h"

DECLARE_STATS_GROUP(TEXT("WaterStatics - BuoyancyData"), STATGROUP_BuoyancyStatics, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("TriangleCreation"), STAT_BuoyantMeshDataTriangleCreation, STATGROUP_BuoyancyStatics);
DECLARE_CYCLE_STAT(TEXT("VertexTransform"), STAT_BuoyantMeshDataVertexTransform, STATGROUP_BuoyancyStatics);
DECLARE_CYCLE_STAT(TEXT("VertexDepthProjection"), STAT_VertexDepthProjection, STATGROUP_BuoyancyStatics);


USTRUCT()
struct FWaterVertex //Data representing an approximated water surface vertex
{
	GENERATED_BODY()
		UPROPERTY()
		FVector Vertex = FVector::ZeroVector;  //The location and water height of a water sample

	FWaterVertex() {};
	FWaterVertex(FVector Vert)
	{
		Vertex = Vert;
	};
};

USTRUCT()
struct FWaterTriangle  //Data representing an approximated water surface triangle
{
	GENERATED_BODY()
		//UPROPERTY()
		TArray<FWaterVertex*> Vertices; //An array of vertices with each element pointing the FWaterGrid's Vertices Array

	FWaterTriangle() {};
	FWaterTriangle(TArray<FWaterVertex*> Verts)
	{
		Vertices = Verts;
	}
	FWaterTriangle(FWaterVertex* VertA, FWaterVertex* VertB, FWaterVertex* VertC)
	{
		Vertices.Add(VertA);
		Vertices.Add(VertB);
		Vertices.Add(VertC);
	};

	/*
	*	Get the Centroid the of this triangle
	*/
	FVector GetCentroid()
	{
		return (Vertices[0]->Vertex + Vertices[1]->Vertex + Vertices[2]->Vertex) / 3.0f;
	}

	/*
	*	Project a point on this triangle and find its depth
	*	@param Point - The point to find the depth of
	*	@return - The depth of the point
	*/
	float GetDepthAtPoint(const FVector& Point)
	{
		FVector WaterPoint = GetProjectedPoint(Point);
		return Point.Z >= WaterPoint.Z ? FMath::Abs(Point.Z - WaterPoint.Z) : -FMath::Abs(Point.Z - WaterPoint.Z);
	}

	/*
	*	Project a point onto this triangle
	*	@param PointToProject - The point to project
	*	@return - The projected point
	*/
	FVector GetProjectedPoint(const FVector& PointToProject)
	{
		return FVector::PointPlaneProject(PointToProject, Vertices[0]->Vertex, Vertices[1]->Vertex, Vertices[2]->Vertex);
	}

	/*
	*	Get the surface normal of this triangle
	*	@return - The surface normal
	*/
	FVector GetSurfaceNormal()
	{
		FVector U = Vertices[1]->Vertex - Vertices[0]->Vertex;
		FVector V = Vertices[2]->Vertex - Vertices[0]->Vertex;
		FVector Normal = FVector::ZeroVector;

		Normal.X = (U.Y * V.Z) - (U.Z - V.Y);
		Normal.Y = (U.Z * V.X) - (U.X - V.Z);
		Normal.Z = (U.X - V.Y) - (U.Y * V.X);

		return Normal;
	}
};

USTRUCT()
struct FWaterCell //Data representing an approximated water surface cell
{
	GENERATED_BODY()
		//UPROPERTY()
		TArray<FWaterVertex*> Vertices; //An array of vertices with each element pointing the FWaterGrid's Vertices Array - 1st vertex is Bottom Left, Last is Upper Right 
	UPROPERTY()
		TArray<FWaterTriangle> Triangles; //The Cell is divided into two triangles, an upper left corner triangle and an lower right corner triangle

	FWaterCell() {};
	FWaterCell(TArray<FWaterVertex*> Verts)
	{
		Vertices = Verts;
		Triangles.Add(FWaterTriangle(Vertices[0], Vertices[1], Vertices[3]));
		Triangles.Add(FWaterTriangle(Vertices[0], Vertices[2], Vertices[3]));
	};

	/*
	*	Find which triangle a given point is inside of
	*	@param CellPoint - the point on the cell to find the triangle it is inside of
	*	@return - the index of the triangle this point is inside of
	*/
	int32 GetTriangleIndexForPoint(const FVector CellPoint)
	{
		if (CellPoint.Y > CellPoint.X)
			return 0;
		else if (CellPoint.Y <= CellPoint.X)
			return 1;

		return 0;
	}
};

//UPDATE_TASK: OPTIMIZATION_UPDATE - local space vertices, pre-allocate memory, change grid points to store height only
USTRUCT()
struct FWaterGrid //Data representing an approximated water surface in a grid system
{
	GENERATED_BODY()
		//UPROPERTY()
		TArray<TArray<FWaterVertex>>Vertices; //A 2D array making up the grid's points - there are always one more set of vertices than cells
											  //UPROPERTY()
	TArray<TArray<FWaterCell>> Cells; //A 2D array containing all of the grid's cells - [Row][Column]
	UPROPERTY()
		FVector GridOrigin = FVector::ZeroVector;  //The lower left corner of this grid used to transform the grid
	UPROPERTY()
		float CellSize = 0.0f; //Length between adjacent vertex points
	UPROPERTY()
		FVector2D GridSize = FVector2D::ZeroVector;	//Represents the number of cells and the last index of the vertices
	UPROPERTY()
		FBox TargetBounds = FBox(); //The bounds of the target this grid is encompassing

	FWaterGrid() {};
	FWaterGrid(float CellLength, FVector BoundingBoxSize, FVector TargetLocation)
	{
		/*
		* The grid's size is the squared rounded up value of the Target's largest BoundingBox length properly snapped
		* to a world space grid consisting of CellLength sized points
		* This allows us to ignore factoring in rotation when updating grid cells
		*/
		float A = BoundingBoxSize.X, B = BoundingBoxSize.Y, C = BoundingBoxSize.Z;
		float TargetSize = FMath::Max3(A, B, C);
		float TargetExtent = TargetSize / 2.0f;
		CellSize = CellLength;
		FVector TargetMin = FVector(-TargetExtent, -TargetExtent, 0.0f);
		FVector TargetMax = FVector(TargetExtent, TargetExtent, 0.0f);
		FBox TargetBox = FBox(TargetMin, TargetMax);
		TargetBounds = TargetBox;
		FBox WorldTargetBox = TargetBox.MoveTo(TargetLocation);
		FVector GridMin = FVector::ZeroVector;
		FVector GridMax = FVector::ZeroVector;
		GridMax.X = TargetBox.Max.X > 0.0f ? FMath::CeilToInt(WorldTargetBox.Max.X / CellSize) : FMath::FloorToInt(WorldTargetBox.Max.X / CellSize);
		GridMin.X = TargetBox.Min.X < 0.0f ? FMath::FloorToInt(WorldTargetBox.Min.X / CellSize) : FMath::CeilToInt(WorldTargetBox.Min.X / CellSize);
		GridMax.Y = TargetBox.Max.Y > 0.0f ? FMath::CeilToInt(WorldTargetBox.Max.Y / CellSize) : FMath::FloorToInt(WorldTargetBox.Max.Y / CellSize);
		GridMin.Y = TargetBox.Min.Y < 0.0f ? FMath::FloorToInt(WorldTargetBox.Min.Y / CellSize) : FMath::CeilToInt(WorldTargetBox.Min.Y / CellSize);
		GridMin *= CellSize;
		GridMax *= CellSize;
		FBox GridBox = FBox(GridMin, GridMax);
		GridOrigin = GridMin;
		GridSize = FVector2D(GridBox.GetSize().X / CellSize, GridBox.GetSize().Y / CellSize);

		Vertices.SetNum(GridSize.X + 1);
		//There is always one more set of rows & cols for vertices 
		for (int r = 0; r < Vertices.Num(); r++)
		{
			Vertices[r].SetNum(GridSize.Y + 1);
			for (int c = 0; c < Vertices[r].Num(); c++)
			{
				FVector NewPoint = GridOrigin;
				NewPoint.X += (CellSize * r);
				NewPoint.Y += (CellSize * c);
				FWaterVertex NewVertex = FWaterVertex(NewPoint);
				Vertices[r][c] = NewVertex;
			}
		}

		Cells.SetNum(GridSize.X);
		for (int r = 0; r < Cells.Num(); r++)
		{
			Cells[r].SetNum(GridSize.Y);
			for (int c = 0; c < Cells[r].Num(); c++)
			{
				TArray<FWaterVertex*> CellVerts = GetCellVerticesForCell(r, c);
				FWaterCell NewCell = FWaterCell(CellVerts);
				Cells[r][c] = NewCell;
			}
		}
	};

	/* Get the grid's bounding box */
	FBox GetWaterGridBoundingBox() { return FBox(Vertices[0][0].Vertex, Vertices[GridSize.X][GridSize.Y].Vertex); }

	/* Get the grid's upper right corner vertex */
	FVector GetRightUpperCornerVertex() { return Vertices[GridSize.X][GridSize.Y].Vertex; }

	/* Get the grid's upper left corner vertex */
	FVector GetLeftUpperCornerVertex() { return Vertices[GridSize.X][0].Vertex; }

	/* Get the grid's lower right corner vertex */
	FVector GetRightLowerCornerVertex() { return Vertices[0][GridSize.Y].Vertex; }

	/* Get the grid's lower left corner vertex */
	FVector GetLeftLowerCornerVertex() { return Vertices[0][0].Vertex; }

	/* Get the grid's horizontal length in cm */
	float GetGridVerticalLength() { return CellSize * GridSize.X; }

	/* Get the grid's horizontal length in cm */
	float GetGridHorizontalLength() { return CellSize * GridSize.Y; }

	TArray<FVector> GetRightUpperGridQuadrant()
	{
		TArray<FVector> Quads;
		return Quads;
	}

	TArray<FVector> GetLeftUpperGridQuadrant()
	{
		TArray<FVector> Quads;
		return Quads;
	}

	TArray<FVector> GetRightLowerGridQuadrant()
	{
		TArray<FVector> Quads;
		return Quads;
	}

	TArray<FVector> GetLeftLowerGridQuadrant()
	{
		TArray<FVector> Quads;
		return Quads;
	}

	/*
	* TODO: Double check the IDs (cell index vs vert index)
	*/
	TArray<FWaterVertex*> GetCellVerticesForCell(const int32 CellRowIndex, const int32 CellColumnIndex)
	{
		TArray<FWaterVertex*> Verts;
		Verts.Add(&Vertices[CellRowIndex][CellColumnIndex]);
		Verts.Add(&Vertices[CellRowIndex][CellColumnIndex + 1]);
		Verts.Add(&Vertices[CellRowIndex + 1][CellColumnIndex]);
		Verts.Add(&Vertices[CellRowIndex + 1][CellColumnIndex + 1]);
		return Verts;
	}

	/*
	*	Get the depth on the grid given a point to project in world space
	*	@param	WorldPoint - The point in world space to project onto the grid to find its depth
	*	@return The depth of the point
	*/
	//TODO: Create actual fix right now we're force clamping the size for intersection to prevent an index crash (bounds sometimes don't resize properly).
	float GetDepthForPoint(const FVector WorldPoint)
	{
		FVector LocalizedGridPoint = FVector(GridOrigin - WorldPoint).GetAbs();
		int CellColumnIndex = FMath::Clamp(FMath::CeilToInt(LocalizedGridPoint.Y / CellSize) - 1, 0, int(GridSize.Y) - 1);
		int CellRowIndex = FMath::Clamp(FMath::CeilToInt(LocalizedGridPoint.X / CellSize) - 1, 0, int(GridSize.X) - 1);
		FVector LocalizedCellPoint = FVector(LocalizedGridPoint.X - (CellRowIndex * CellSize), LocalizedGridPoint.Y - (CellColumnIndex * CellSize), 0.0f);
		return Cells[CellRowIndex][CellColumnIndex].Triangles[Cells[CellRowIndex][CellColumnIndex].GetTriangleIndexForPoint(LocalizedCellPoint)].GetDepthAtPoint(WorldPoint);
	}

	/*
	*	Get information about this grid's water data in a compact form
	*	@return The compact vectorized water data for this grid
	*/
	//TODO make this just a world offset vector, cell size float & height float for each vertex
	TArray<TArray<FVector>> GetWaterData()
	{
		TArray<TArray<FVector>> WaterData;
		WaterData.SetNum(Vertices.Num());
		for (int RowIndex = 0; RowIndex < Vertices.Num(); RowIndex++)
		{
			for (int ColIndex = 0; ColIndex < Vertices[RowIndex].Num(); ColIndex++)
			{
				WaterData[RowIndex].Add(Vertices[RowIndex][ColIndex].Vertex);
			}
		}

		return WaterData;
	}
};

USTRUCT()
struct FMeshVertex //Data representing a single vertex in local space
{
	GENERATED_BODY()
		UPROPERTY()
		FVector Vertex = FVector::ZeroVector; //The location of a vertex on a mesh

	FMeshVertex() {};
	FMeshVertex(FVector Vert)
	{
		Vertex = Vert;
	};
};

USTRUCT()
struct FMeshTriangle  //Data representing a triangle consisting of three vertices in local space
{
	GENERATED_BODY()
		//UPROPERTY()
		TArray<FMeshVertex*> Vertices; //Stores pointers to an element of the FMeshData's Vertices Array

	UPROPERTY()
		float Area = 0.0f; //The surface area of this triangle, in centimeters

	FMeshTriangle() {};
	FMeshTriangle(FMeshVertex* VertA, FMeshVertex* VertB, FMeshVertex* VertC)
	{
		Vertices.Add(VertA);
		Vertices.Add(VertB);
		Vertices.Add(VertC);

		const FVector AB = Vertices[0]->Vertex - Vertices[1]->Vertex;
		const FVector AC = Vertices[0]->Vertex - Vertices[2]->Vertex;
		Area = (AB.Size() * AC.Size()) * FGenericPlatformMath::Sin(FGenericPlatformMath::Acos(FVector::DotProduct(AB.GetSafeNormal(), AC.GetSafeNormal()))) / 2.0f;
	};
	FMeshTriangle(TArray<FMeshVertex*>Verts)
	{
		Vertices = Verts;
		const FVector AB = Vertices[0]->Vertex - Vertices[1]->Vertex;
		const FVector AC = Vertices[0]->Vertex - Vertices[2]->Vertex;
		Area = (AB.Size() * AC.Size()) * FGenericPlatformMath::Sin(FGenericPlatformMath::Acos(FVector::DotProduct(AB.GetSafeNormal(), AC.GetSafeNormal()))) / 2.0f;
	};
};

USTRUCT()
struct FMeshData //Data representing a mesh consisting of vertices and triangles in local space
{
	GENERATED_BODY()
		UPROPERTY()
		TArray<int32> Vertices; //An array containing indices to all of the vertices
	UPROPERTY()
		TArray<FMeshVertex> UniqueVertices;  //An array containing all of the triangle's unique vertices
	UPROPERTY()
		TArray<FMeshTriangle> Triangles;  //An array containing all of the Mesh's triangles
	UPROPERTY()
		float TotalSurfaceArea = 0.0f;

	FMeshData() {};
	FMeshData(TArray<FMeshVertex> RawVertices)
	{
		for (int VertIndex = 0; VertIndex < RawVertices.Num(); VertIndex++)
		{
			int32 OutVertexIndex;
			bool bFoundDuplicate = false;
			for (int UniqueVertexIndex = 0; UniqueVertexIndex < UniqueVertices.Num(); UniqueVertexIndex++)
			{
				if (UniqueVertices[UniqueVertexIndex].Vertex.Equals(RawVertices[VertIndex].Vertex))
				{
					bFoundDuplicate = true;
					OutVertexIndex = UniqueVertexIndex;
					break;
				}
			}
			if (!bFoundDuplicate)
			{
				UniqueVertices.Add(RawVertices[VertIndex]);
				Vertices.Add(UniqueVertices.Num() - 1);
			}
			else
			{
				Vertices.Add(OutVertexIndex);
			}
		}

		for (int TriIndex = 0; TriIndex < Vertices.Num() / 3; TriIndex++)
		{
			int32 IndexOne = Vertices[TriIndex * 3], IndexTwo = Vertices[TriIndex * 3 + 1], IndexThree = Vertices[TriIndex * 3 + 2];
			FMeshTriangle NewTriangle = FMeshTriangle(&UniqueVertices[IndexOne], &UniqueVertices[IndexTwo], &UniqueVertices[IndexThree]);
			TotalSurfaceArea += NewTriangle.Area;
			Triangles.Add(NewTriangle);
		}
	};
};

USTRUCT()
struct FBuoyantVertex  //Data representing a single vertex  and information about depth in world space
{
	GENERATED_BODY()
		UPROPERTY()
		FVector Vertex = FVector::ZeroVector; //The location of a vertex on a mesh in world space
	UPROPERTY()
		float Depth = 0.0f;  //The depth of this vertex relative to the water's height at the vertex

	FBuoyantVertex() {};
	FBuoyantVertex(FVector Vert)
	{
		Vertex = Vert;
	};
	FBuoyantVertex(FVector Vert, float H)
	{
		Vertex = Vert;
		Depth = H;
	};

	/*
	* Returns true if the vertex is submerged
	*/
	bool IsSubmerged()
	{
		return Depth < 0.0f;
	}
};

//UPDATE_TASK: OPTIMIZATION_UPDATE - Store Vertices as pointers, pre-allocate memory
USTRUCT()
struct FBuoyantTriangle //Data representing a triangle consisting of three vertices and information about depth in world space
{
	GENERATED_BODY()
		UPROPERTY()
		TArray<FBuoyantVertex> Vertices; //TODO: MAKE POINTERS
	UPROPERTY()
		float Area = 0.0f; //Surface Area in cm
	UPROPERTY()
		float CutSubmergedArea = 0.0f; //TODO: REFACTOR
	UPROPERTY()
		FVector Center = FVector::ZeroVector; //The center (centroid) of the triangle
	UPROPERTY()
		FVector ForceCenter = FVector::ZeroVector; // The "center" where hydrostatic forces are applied to the triangle
	UPROPERTY()
		FVector OutwardNormal = FVector::ZeroVector; //The triangle's outward facing normal
	UPROPERTY()
		FVector HydrostaticForce = FVector::ZeroVector; //The hydrostatic (buoyant force) force applied to the triangle's force center
	UPROPERTY()
		FVector PressureDragForce = FVector::ZeroVector; //Pressure Drag force - a hydrodynamic damping force applied to the triangle's center. Captures planing forces and forces allowing the boat to turn.
	UPROPERTY()
		FVector WaterResistanceForce = FVector::ZeroVector; //Water viscous resistance force - a hydrodynamic damping force applied to the triangle's center
	UPROPERTY()
		FVector WaterEntryForce = FVector::ZeroVector; //Water "slamming" entry force - a hydrodynamic damping force applied to the triangle's center
	UPROPERTY()
		bool bHorizontalEdgePointsUp = false; //True if this triangle's horizontal edge "points" up
	UPROPERTY()
		float Depth = 0.0f; //The depth of this triangle at its center
	UPROPERTY()
		FVector Velocity = FVector::ZeroVector; //Velocity of the triangle at its center

	FBuoyantTriangle() {};

	FBuoyantTriangle(FBuoyantVertex VertA, FBuoyantVertex VertB, FBuoyantVertex VertC)
	{
		Vertices.Add(VertA);
		Vertices.Add(VertB);
		Vertices.Add(VertC);
		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			for (int32 j = i + 1; j < Vertices.Num(); j++)
			{
				if (Vertices[i].Depth < Vertices[j].Depth)
					Vertices.Swap(i, j);
			}
		}
		Center = (Vertices[0].Vertex + Vertices[1].Vertex + Vertices[2].Vertex) / 3.0f;
		FVector AB = Vertices[0].Vertex - Vertices[1].Vertex;
		FVector AC = Vertices[0].Vertex - Vertices[2].Vertex;
		Area = (AB.Size() * AC.Size()) * FGenericPlatformMath::Sin(FGenericPlatformMath::Acos(FVector::DotProduct(AB.GetSafeNormal(), AC.GetSafeNormal()))) / 2.0f;
	};

	FBuoyantTriangle(FBuoyantVertex VertA, FBuoyantVertex VertB, FBuoyantVertex VertC, float TriDepth, const FBodyInstance* BodyInstance, FVector MeshCenter = FVector::ZeroVector)
	{
		Vertices.Add(VertA);
		Vertices.Add(VertB);
		Vertices.Add(VertC);
		Center = (Vertices[0].Vertex + Vertices[1].Vertex + Vertices[2].Vertex) / 3.0f;
		Depth = TriDepth;

		FVector AB = Vertices[0].Vertex - Vertices[1].Vertex;
		FVector AC = Vertices[0].Vertex - Vertices[2].Vertex;
		Area = (AB.Size() * AC.Size()) * FGenericPlatformMath::Sin(FGenericPlatformMath::Acos(FVector::DotProduct(AB.GetSafeNormal(), AC.GetSafeNormal()))) / 2.0f;

		//Find the proper normal based on Mesh's center.
		//Verts are in the correct order if DP is positive, otherwise switch the vertices
		const FVector Direction = Center - MeshCenter;
		OutwardNormal = FVector::CrossProduct((Vertices[1].Vertex - Vertices[0].Vertex), Vertices[2].Vertex - Vertices[0].Vertex);
		if (FVector::DotProduct(OutwardNormal, Direction) < 0.0f)
			OutwardNormal = FVector::CrossProduct((Vertices[1].Vertex - Vertices[2].Vertex), Vertices[0].Vertex - Vertices[2].Vertex);
		OutwardNormal.Normalize();

		Velocity = BodyInstance->GetUnrealWorldVelocity_AssumesLocked() + (FVector::CrossProduct(FMath::RadiansToDegrees(BodyInstance->GetUnrealWorldAngularVelocityInRadians_AssumesLocked()), Center - BodyInstance->GetCOMPosition()));
		//Velocity = BodyInstance->GetUnrealWorldVelocityAtPoint_AssumesLocked(Center);
	};

	FBuoyantTriangle(FBuoyantVertex VertA, FBuoyantVertex VertB, FBuoyantVertex VertC, float TriDepth, bool bPointsUp, const FBodyInstance* BodyInstance, FVector MeshCenter = FVector::ZeroVector)
	{
		Vertices.Add(VertA);
		Vertices.Add(VertB);
		Vertices.Add(VertC);
		Center = (Vertices[0].Vertex + Vertices[1].Vertex + Vertices[2].Vertex) / 3.0f;
		Depth = TriDepth;

		FVector AB = Vertices[0].Vertex - Vertices[1].Vertex;
		FVector AC = Vertices[0].Vertex - Vertices[2].Vertex;
		Area = (AB.Size() * AC.Size()) * FGenericPlatformMath::Sin(FGenericPlatformMath::Acos(FVector::DotProduct(AB.GetSafeNormal(), AC.GetSafeNormal()))) / 2.0f;

		//Find the proper normal based on Mesh's center.
		//Verts are in the correct order if DP is positive, otherwise switch the vertices
		const FVector Direction = Center - MeshCenter;
		OutwardNormal = FVector::CrossProduct((Vertices[1].Vertex - Vertices[0].Vertex), Vertices[2].Vertex - Vertices[0].Vertex);
		if (FVector::DotProduct(OutwardNormal, Direction) < 0.0f)
			OutwardNormal = FVector::CrossProduct((Vertices[1].Vertex - Vertices[2].Vertex), Vertices[0].Vertex - Vertices[2].Vertex);
		OutwardNormal.Normalize();

		if (bHorizontalEdgePointsUp)
		{
			FVector ForcePoint = (VertB.Vertex + VertC.Vertex) / 2;
			FVector ForceLine = VertA.Vertex - ForcePoint;
			float Height = FMath::Abs(VertA.Vertex.Z - ForcePoint.Z);
			ForceCenter = ForcePoint + ForceLine * (2 * FMath::Abs(ForcePoint.Z) + Height) / (6 * FMath::Abs(ForcePoint.Z) + 2 * Height);
		}
		else
		{
			FVector ForcePoint = (VertB.Vertex + VertC.Vertex) / 2;
			FVector ForceLine = ForcePoint - VertA.Vertex;
			float height = FMath::Abs(VertA.Vertex.Z - ForcePoint.Z);
			ForceCenter = VertA.Vertex + ForceLine * (4 * FMath::Abs(VertA.Depth) + 3 * height) / (6 * FMath::Abs(VertA.Depth) + 4 * height);
		}

		Velocity = BodyInstance->GetUnrealWorldVelocity_AssumesLocked() + (FVector::CrossProduct(FMath::RadiansToDegrees(BodyInstance->GetUnrealWorldAngularVelocityInRadians_AssumesLocked()), Center - BodyInstance->GetCOMPosition()));
		//Velocity = BodyInstance->GetUnrealWorldVelocityAtPoint_AssumesLocked(Center);
	};

	/*
	*	Returns true if this triangle is fully submerged (all vertices are submerged).
	*/
	bool IsSubmerged()
	{
		return Vertices[0].IsSubmerged() && Vertices[1].IsSubmerged() && Vertices[2].IsSubmerged();
	}

	/*
	*	Returns true if this triangle is fully surfaced (all vertices are surfaced).
	*/
	bool IsSurfaced()
	{
		return !Vertices[0].IsSubmerged() && !Vertices[1].IsSubmerged() && !Vertices[2].IsSubmerged();
	}

	/*
	* Returns a copy of the vertices array ordered by depth - deepest first, shallowest last.
	*/
	TArray<FBuoyantVertex> GetVerticesByDepth() const
	{
		TArray<FBuoyantVertex>VertsByDepth = Vertices;
		for (int32 i = 0; i < VertsByDepth.Num(); i++)
		{
			for (int32 j = i + 1; j < VertsByDepth.Num(); j++)
			{
				if (VertsByDepth[i].Depth < VertsByDepth[j].Depth)
					VertsByDepth.Swap(i, j);
			}
		}

		return VertsByDepth;
	}

	/*
	* Returns a copy of the vertices array ordered by height - highest first, lowest last.
	*/
	TArray<FBuoyantVertex> GetVerticesByHeight() const
	{
		TArray<FBuoyantVertex>VertsByHeight = Vertices;

		for (int32 i = 0; i < VertsByHeight.Num(); i++)
		{
			for (int32 j = i + 1; j < VertsByHeight.Num(); j++)
			{
				if (VertsByHeight[i].Vertex.Z < VertsByHeight[j].Vertex.Z)
					VertsByHeight.Swap(i, j);
			}
		}
		return VertsByHeight;
	}

	/*
	* Returns the water direction "theta"
	* @return - Returns the dot product between the normal and velocity normal
	*/
	float GetWaterDirection() const
	{
		return FVector::DotProduct(Velocity.GetUnsafeNormal(), OutwardNormal);
	}
};

//UPDATE_TASK: OPTIMIZATION_UPDATE - Pre-allocate memory
USTRUCT()
struct FBuoyantMeshData //Data representing a mesh consisting of vertices and triangles divided into submerged and surfaced sections in world space
{
	GENERATED_BODY()
		UPROPERTY()
		TArray<int32> Vertices;
	UPROPERTY()
		TArray<FBuoyantVertex> UniqueVertices; //The unique vertices of this mesh, these vertices have their depth updated each sub frame
	UPROPERTY()
		TArray<FBuoyantTriangle> Triangles; //The triangles of this mesh, these triangles are in world space
	UPROPERTY()
		TArray<FBuoyantTriangle> SubmergedTriangles;  //The submerged triangles of this mesh 
	//UPDATE_TASK: AIR_DRAG - Re-implement surfaced triangles for air drag
	//UPROPERTY()
	//TArray<FBuoyantTriangle> SurfacedTriangles; //Removed, but useful for air drag 
	UPROPERTY()
		TArray<FBuoyantVertex> WaterLineVertices;
	UPROPERTY()
		float TotalSurfaceAreaSubmerged = 0.0f; //The total area of the mesh submerged, used to scale damping forces
	FBuoyantMeshData() {};

	FBuoyantMeshData(const TArray<FMeshVertex>UniqueMeshVertices, const TArray<int32>MeshIndices, FWaterGrid& WaterGrid, const FTransform& Transform, const FBodyInstance* BodyInstance)
	{
		SCOPE_CYCLE_COUNTER(STAT_BuoyantMeshDataVertexTransform)
		{
			for (const FMeshVertex& UniqueMeshVertex : UniqueMeshVertices)
			{
				/*
				* Performance increase, ignore scale and don't check NaN
				*/
				FBuoyantVertex BuoyantVertex = FBuoyantVertex(Transform.GetRotation().RotateVector(UniqueMeshVertex.Vertex) + Transform.GetTranslation());
				UniqueVertices.Add(BuoyantVertex);
			}
		}

		SCOPE_CYCLE_COUNTER(STAT_VertexDepthProjection)
		{
			//UPDATE_TASK: OPTIMIZATION_UPDATE - Find cheaper alternative to PointPlaneProject This has a large cost associated with it.
			for (FBuoyantVertex& UniqueVertex : UniqueVertices)
			{
				UniqueVertex.Depth = WaterGrid.GetDepthForPoint(UniqueVertex.Vertex);
			}
		}

		Vertices = MeshIndices;
		SCOPE_CYCLE_COUNTER(STAT_BuoyantMeshDataTriangleCreation)
		{
			for (int TriIndex = 0; TriIndex < Vertices.Num() / 3; TriIndex++)
			{
				int32 IndexOne = Vertices[TriIndex * 3], IndexTwo = Vertices[TriIndex * 3 + 1], IndexThree = Vertices[TriIndex * 3 + 2];
				FVector TriCenter = (UniqueVertices[IndexOne].Vertex + UniqueVertices[IndexTwo].Vertex + UniqueVertices[IndexThree].Vertex) / 3.0f;
				FBuoyantTriangle NewTriangle = FBuoyantTriangle(UniqueVertices[IndexOne], UniqueVertices[IndexTwo], UniqueVertices[IndexThree], WaterGrid.GetDepthForPoint(TriCenter), BodyInstance, Transform.GetLocation());
				Triangles.Add(NewTriangle);
			}
		}
	};

	void Clear()
	{
		Vertices.Empty();
		Triangles.Empty();
		SubmergedTriangles.Empty();
		//UPDATE_TASK: AIR_DRAG
		//SurfacedTriangles.Empty();
		WaterLineVertices.Empty();
		Vertices.Empty();
	}
};

//UPDATE_TASK: OPTIMIZATION_UPDATE - Pre-allocate memory
USTRUCT()
struct FBuoyancyFrameData //Buoyancy information for a mesh over a single frame
{
	GENERATED_BODY()

	UPROPERTY()
		FBuoyantMeshData BuoyantData; //Buoyant Mesh data for this frame
	UPROPERTY()
		FVector CumulativeHydrostaticForces = FVector::ZeroVector; //The cumulative hydrostatic force from all submerged triangles
	UPROPERTY()
		FVector CumulativeWaterResistanceForce = FVector::ZeroVector; //The cumulative water resistance force from all submerged triangles
	UPROPERTY()
		FVector CumulativePressureDragForces = FVector::ZeroVector; //The cumulative hydrostatic force from all submerged triangles
	UPROPERTY()
		FVector CumulativeWaterEntryForces = FVector::ZeroVector; //The cumulative hydrostatic force from all submerged triangles
	UPROPERTY()
		FTransform BodyTransform = FTransform(); //Transform of the bodyinstance in this frame
	UPROPERTY()
		float DeltaTime = 0.0f; //Frame delta time 

	FBuoyancyFrameData() {};
	FBuoyancyFrameData(float SubstepDeltaTime)
	{
		DeltaTime = SubstepDeltaTime;
	}

	void Clear()
	{
		BuoyantData.Clear();
		DeltaTime = 0.0f;
	}
};

//UPDATE_TASK: OPTIMIZATION_UPDATE - Pre-allocate memory
USTRUCT()
struct FBuoyancyData //Buoyancy information for a mesh over a set period of time 
{
	GENERATED_BODY()

	UPROPERTY()
		TArray<FBuoyancyFrameData>SubFrameCircularBuffer; //Buffer containing the current sub frame and the last sub frame's buoyancy data
	UPROPERTY()
		FMeshData MeshData; //Information about the buoyant mesh's static mesh used for creation of buoyancy information

	FBuoyancyData() {};
	FBuoyancyData(uint32 FrameBufferLength, FMeshData Data)
	{
		MeshData = Data;
		SubFrameCircularBuffer.SetNum(2);
	};
};

UCLASS()
class UBuoyancyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	//UPDATE_TASK: REFACTOR_CLEANUP - Move cutting and splitting algorithms here
};

