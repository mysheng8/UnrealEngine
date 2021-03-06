/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_PHYSICS_SCP_CONSTRAINT_PROJECTION_MANAGER
#define PX_PHYSICS_SCP_CONSTRAINT_PROJECTION_MANAGER

#include "PsPool.h"
#include "ScConstraintGroupNode.h"

namespace physx
{
namespace Sc
{
	class ConstraintSim;
	class BodySim;

	class ConstraintProjectionManager : public Ps::UserAllocated
	{
	public:
		ConstraintProjectionManager();
		~ConstraintProjectionManager() {}

		void addToPendingGroupUpdates(ConstraintSim& s);
		void removeFromPendingGroupUpdates(ConstraintSim& s);

		void buildGroups();
		void invalidateGroup(ConstraintGroupNode& node, ConstraintSim* constraintDeleted);

	private:
		PX_INLINE Sc::ConstraintGroupNode* createGroupNode(BodySim& b);

		void addToGroup(BodySim& b, BodySim* other, ConstraintSim& c);
		void groupUnion(ConstraintGroupNode& root0, ConstraintGroupNode& root1);
		void dumpConnectedConstraints(BodySim& b, ConstraintSim* c, bool projConstrOnly);


	private:
		Ps::Pool<ConstraintGroupNode>	mNodePool;
		Ps::Array<ConstraintSim*>		mPendingGroupUpdates; //list of constraints for which constraint projection groups need to be generated/updated
	};

} // namespace Sc

}

#endif
