// Fourfold - native animation runtime: UFourfoldAnimInstance + FFourfoldAnimProxy (see FourfoldAnimInstance.h).
// Verified pattern (open-source UE5 projects + 5.5 headers, docs/game/API_NOTES.md): Evaluate() returning true skips
// the (absent) node graph; clips are sampled with UAnimSequence::GetAnimationPose(FAnimationPoseData(FPoseContext),
// FAnimExtractContext(double time, false, FDeltaTimeRecord(), bLooping)); bone transforms are blended by hand.
// All procedural math lives in the logic island (FFGIK / FFGSprings / FFGInertial), unit-tested in the container.
#include "FourfoldAnimInstance.h"

#include "Logic/FFGIK.h"
#include "Logic/FFGInertial.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "BoneContainer.h"
#include "BonePose.h"

namespace FourfoldAnimRt
{
	inline ffg::Quat ToQ(const FQuat& Q) { return ffg::Quat(float(Q.X), float(Q.Y), float(Q.Z), float(Q.W)); }
	inline FQuat FromQ(const ffg::Quat& Q) { return FQuat(double(Q.x), double(Q.y), double(Q.z), double(Q.w)); }
	inline ffg::Vec3 ToV(const FVector& V, double Scale) { return ffg::Vec3(float(V.X * Scale), float(V.Y * Scale), float(V.Z * Scale)); }
	inline FVector FromV(const ffg::Vec3& V, double Scale) { return FVector(double(V.x) * Scale, double(V.y) * Scale, double(V.z) * Scale); }
	constexpr double CmToM = 0.01;
	constexpr double MToCm = 100.0;

	/** Ground of the analytic sim arena under a world point (UE world in metres: X = sim x, Y = sim z, Z = sim y). */
	class FArenaGroundSampler final : public ffg::GroundSampler
	{
	public:
		explicit FArenaGroundSampler(const ffg::ArenaGround& InArena) : Arena(InArena) {}
		virtual float GroundUp(ffg::Vec3 WorldPoint, float FromUp) const override
		{
			return Arena.GroundHeight(WorldPoint.x, WorldPoint.y, FromUp, ffg::ArenaGround::kStepHeight);
		}

	private:
		const ffg::ArenaGround& Arena;
	};

	struct FChain
	{
		TArray<int32> Bones;            // compact indices, root first
		ffg::Vec3 TailLocal;            // virtual tail of the last bone in its own frame (metres)
		ffg::SpringChain Sim;
		bool bThighCollide = false;
	};
}

/** The worker-thread side. Persistent state: bone cache, last clip pose (cross-fades), foot planter, look-at, chains. */
struct FFourfoldAnimProxy final : public FAnimInstanceProxy
{
	explicit FFourfoldAnimProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	FFourfoldAnimFrame Frame;
	bool bHaveFrame = false;

	// ---- bone cache (rebuilt when the required bones change: mesh, LOD)
	uint16 CacheSerial = MAX_uint16;
	int32 CacheNum = -1;
	TArray<int32> Parent;
	int32 Pelvis = INDEX_NONE, Head = INDEX_NONE;
	int32 Spine[5] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 Neck[2] = {INDEX_NONE, INDEX_NONE};
	int32 Clav[2] = {INDEX_NONE, INDEX_NONE};
	int32 UpperArm[2] = {INDEX_NONE, INDEX_NONE};
	int32 Thigh[2] = {INDEX_NONE, INDEX_NONE};
	int32 Calf[2] = {INDEX_NONE, INDEX_NONE};
	int32 Foot[2] = {INDEX_NONE, INDEX_NONE};
	int32 Ball[2] = {INDEX_NONE, INDEX_NONE};
	TArray<int32> Fingers[2];
	TArray<uint8> LegMask;
	TArray<FourfoldAnimRt::FChain> Chains;
	ffg::Vec3 HeadFwdLocal{0.0f, 1.0f, 0.0f};
	float LegLen[2] = {0.82f, 0.82f};

	// ---- scratch + state
	TArray<ffg::Xform> Local, CS, LastClip;
	TArray<FVector> Scales;
	TArray<FQuat> AccQ;
	TArray<FVector> AccT, AccS;
	uint32 LastTransitionSerial = 0;
	ffg::InertialBlend Inertial;
	ffg::FootPlanter Planter;
	ffg::LookAt Look;

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
		UFourfoldAnimInstance* Inst = Cast<UFourfoldAnimInstance>(InAnimInstance);
		if (Inst && Inst->bHasPending)
		{
			Frame = Inst->Pending;
			Inst->Pending.bReset = false;   // a reset is consumed once
			bHaveFrame = true;
		}
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		FCompactPose& Pose = Output.Pose;
		const int32 N = Pose.GetNumBones();
		if (N <= 0)
		{
			return true;
		}
		const FBoneContainer& BC = Pose.GetBoneContainer();
		if (CacheSerial != BC.GetSerialNumber() || CacheNum != N)
		{
			RebuildCache(Pose);
		}
		if (!bHaveFrame || Frame.Base.Num() == 0)
		{
			Output.ResetToRefPose();
		}
		else
		{
			BlendClips(Frame.Base, Output);
		}
		if (bHaveFrame)
		{
			ApplyLegs(Output);
			ApplyAdditive(Output);
			ApplyHands(Output);
		}

		// Local transforms -> logic-island form (metres), cross-fade, procedural layers.
		Local.SetNum(N, EAllowShrinking::No);
		Scales.SetNum(N, EAllowShrinking::No);
		for (int32 i = 0; i < N; ++i)
		{
			const FTransform& T = Pose[FCompactPoseBoneIndex(i)];
			Local[i].q = FourfoldAnimRt::ToQ(T.GetRotation());
			Local[i].t = FourfoldAnimRt::ToV(T.GetTranslation(), FourfoldAnimRt::CmToM);
			Scales[i] = T.GetScale3D();
		}
		if (bHaveFrame)
		{
			if (Frame.bReset)
			{
				Inertial.Stop();
				Planter.Reset();
				Look.Reset();
				for (FourfoldAnimRt::FChain& C : Chains)
				{
					C.Sim.Reset();
				}
				LastClip.Reset();
			}
			if (Frame.TransitionSerial != LastTransitionSerial)
			{
				if (LastClip.Num() == N)
				{
					Inertial.Start(LastClip.GetData(), Local.GetData(), size_t(N), Frame.TransitionTime);
				}
				LastTransitionSerial = Frame.TransitionSerial;
			}
			Inertial.Apply(Local.GetData(), size_t(N), Frame.Dt);
			LastClip = Local;
			if (Frame.Lod < 2)
			{
				Procedural();
			}
		}
		for (int32 i = 0; i < N; ++i)
		{
			FTransform& T = Pose[FCompactPoseBoneIndex(i)];
			T.SetRotation(FourfoldAnimRt::FromQ(Local[i].q).GetNormalized());
			T.SetTranslation(FourfoldAnimRt::FromV(Local[i].t, FourfoldAnimRt::MToCm));
			T.SetScale3D(Scales[i]);
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ clips

	static void Sample(const FFourfoldClipRef& C, FPoseContext& Ctx)
	{
		Ctx.ResetToRefPose();
		if (!C.Seq)
		{
			return;
		}
		FAnimationPoseData Data(Ctx);
		const double Len = C.Seq->GetPlayLength();
		const double Time = FMath::Clamp(double(C.Time), 0.0, FMath::Max(Len, 0.0));
		C.Seq->GetAnimationPose(Data, FAnimExtractContext(Time, false, FDeltaTimeRecord(), C.bLoop));
	}

	/** Weighted average of every entry (quaternions sign-aligned, normalised). */
	void BlendClips(const TArray<FFourfoldClipRef, TInlineAllocator<6>>& Entries, FPoseContext& Output)
	{
		float WSum = 0.0f;
		int32 Valid = 0;
		for (const FFourfoldClipRef& E : Entries)
		{
			if (E.Seq && E.Weight > 1e-4f)
			{
				WSum += E.Weight;
				++Valid;
			}
		}
		if (Valid == 0)
		{
			Output.ResetToRefPose();
			return;
		}
		if (Valid == 1)
		{
			for (const FFourfoldClipRef& E : Entries)
			{
				if (E.Seq && E.Weight > 1e-4f)
				{
					Sample(E, Output);
				}
			}
			return;
		}
		const int32 N = Output.Pose.GetNumBones();
		AccQ.SetNum(N, EAllowShrinking::No);
		AccT.SetNum(N, EAllowShrinking::No);
		AccS.SetNum(N, EAllowShrinking::No);
		for (int32 i = 0; i < N; ++i)
		{
			AccQ[i] = FQuat(0.0, 0.0, 0.0, 0.0);
			AccT[i] = FVector::ZeroVector;
			AccS[i] = FVector::ZeroVector;
		}
		FPoseContext Tmp(Output);
		bool bFirst = true;
		for (const FFourfoldClipRef& E : Entries)
		{
			if (!E.Seq || E.Weight <= 1e-4f)
			{
				continue;
			}
			Sample(E, Tmp);
			const double W = double(E.Weight / WSum);
			for (int32 i = 0; i < N; ++i)
			{
				const FTransform& T = Tmp.Pose[FCompactPoseBoneIndex(i)];
				FQuat Q = T.GetRotation();
				if (!bFirst && (AccQ[i] | Q) < 0.0)
				{
					Q = -Q;
				}
				AccQ[i] += Q * W;
				AccT[i] += T.GetTranslation() * W;
				AccS[i] += T.GetScale3D() * W;
			}
			bFirst = false;
		}
		for (int32 i = 0; i < N; ++i)
		{
			Output.Pose[FCompactPoseBoneIndex(i)] = FTransform(AccQ[i].GetNormalized(), AccT[i], AccS[i]);
		}
	}

	void ApplyLegs(FPoseContext& Output)
	{
		if (Frame.LegsWeight <= 0.01f || Frame.Legs.Num() == 0 || LegMask.Num() != Output.Pose.GetNumBones())
		{
			return;
		}
		FPoseContext Tmp(Output);
		BlendClips(Frame.Legs, Tmp);
		const float W = FMath::Clamp(Frame.LegsWeight, 0.0f, 1.0f);
		for (int32 i = 0; i < LegMask.Num(); ++i)
		{
			if (!LegMask[i])
			{
				continue;
			}
			FTransform& A = Output.Pose[FCompactPoseBoneIndex(i)];
			const FTransform& B = Tmp.Pose[FCompactPoseBoneIndex(i)];
			A.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), W).GetNormalized());
			A.SetTranslation(FMath::Lerp(A.GetTranslation(), B.GetTranslation(), double(W)));
		}
	}

	void ApplyAdditive(FPoseContext& Output)
	{
		const FFourfoldClipRef& A = Frame.Additive;
		if (!A.Seq || A.Weight <= 1e-3f)
		{
			return;
		}
		FPoseContext At(Output);
		FPoseContext Zero(Output);
		Sample(A, At);
		FFourfoldClipRef Z = A;
		Z.Time = 0.0f;
		Sample(Z, Zero);
		const float W = FMath::Clamp(A.Weight, 0.0f, 1.0f);
		const int32 N = Output.Pose.GetNumBones();
		for (int32 i = 1; i < N; ++i)   // never the root
		{
			const FCompactPoseBoneIndex Bi(i);
			const FQuat Delta = (At.Pose[Bi].GetRotation() * Zero.Pose[Bi].GetRotation().Inverse()).GetNormalized();
			FTransform& T = Output.Pose[Bi];
			T.SetRotation((FQuat::Slerp(FQuat::Identity, Delta, W) * T.GetRotation()).GetNormalized());
			T.AddToTranslation((At.Pose[Bi].GetTranslation() - Zero.Pose[Bi].GetTranslation()) * double(W));
		}
	}

	void ApplyHands(FPoseContext& Output)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FFourfoldClipRef& H = Frame.Hand[Side];
			if (!H.Seq || H.Weight <= 1e-3f || Fingers[Side].Num() == 0)
			{
				continue;
			}
			FPoseContext Tmp(Output);
			FFourfoldClipRef Z = H;
			Z.Time = 0.0f;
			Sample(Z, Tmp);
			const float W = FMath::Clamp(H.Weight, 0.0f, 1.0f);
			for (int32 i : Fingers[Side])
			{
				const FCompactPoseBoneIndex Bi(i);
				FTransform& T = Output.Pose[Bi];
				T.SetRotation(FQuat::Slerp(T.GetRotation(), Tmp.Pose[Bi].GetRotation(), W).GetNormalized());
			}
		}
	}

	// ------------------------------------------------------------------------------------------------ bone cache

	void RebuildCache(const FCompactPose& Pose)
	{
		const FBoneContainer& BC = Pose.GetBoneContainer();
		const FReferenceSkeleton& Ref = BC.GetReferenceSkeleton();
		const int32 N = Pose.GetNumBones();
		CacheSerial = BC.GetSerialNumber();
		CacheNum = N;
		Parent.SetNum(N);
		TMap<FName, int32> ByName;
		for (int32 i = 0; i < N; ++i)
		{
			const FCompactPoseBoneIndex Bi(i);
			const FCompactPoseBoneIndex P = BC.GetParentBoneIndex(Bi);
			Parent[i] = P.GetInt();
			ByName.Add(Ref.GetBoneName(BC.MakeMeshPoseIndex(Bi).GetInt()), i);
		}
		auto Find = [&ByName](const TCHAR* Name) {
			const int32* I = ByName.Find(FName(Name));
			return I ? *I : INDEX_NONE;
		};
		Pelvis = Find(TEXT("pelvis"));
		Head = Find(TEXT("head"));
		const TCHAR* SpineNames[5] = {TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05")};
		for (int32 k = 0; k < 5; ++k)
		{
			Spine[k] = Find(SpineNames[k]);
		}
		Neck[0] = Find(TEXT("neck_01"));
		Neck[1] = Find(TEXT("neck_02"));
		const TCHAR* SideSuffix[2] = {TEXT("l"), TEXT("r")};
		LegMask.Init(0, N);
		for (int32 s = 0; s < 2; ++s)
		{
			auto F = [&](const TCHAR* Base) { return Find(*FString::Printf(TEXT("%s_%s"), Base, SideSuffix[s])); };
			Clav[s] = F(TEXT("clavicle"));
			UpperArm[s] = F(TEXT("upperarm"));
			Thigh[s] = F(TEXT("thigh"));
			Calf[s] = F(TEXT("calf"));
			Foot[s] = F(TEXT("foot"));
			Ball[s] = F(TEXT("ball"));
			for (const TCHAR* Leg : {TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball"), TEXT("thigh_twist_01"), TEXT("thigh_twist_02"),
			                         TEXT("calf_twist_01"), TEXT("calf_twist_02")})
			{
				const int32 I = F(Leg);
				if (I != INDEX_NONE)
				{
					LegMask[I] = 1;
				}
			}
			Fingers[s].Reset();
			for (const TCHAR* Fn : {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")})
			{
				for (const TCHAR* Part : {TEXT("metacarpal"), TEXT("01"), TEXT("02"), TEXT("03")})
				{
					const int32 I = Find(*FString::Printf(TEXT("%s_%s_%s"), Fn, Part, SideSuffix[s]));
					if (I != INDEX_NONE)
					{
						Fingers[s].Add(I);
					}
				}
			}
			for (const TCHAR* Part : {TEXT("thumb_01"), TEXT("thumb_02"), TEXT("thumb_03")})
			{
				const int32 I = F(Part);
				if (I != INDEX_NONE)
				{
					Fingers[s].Add(I);
				}
			}
		}
		// Reference pose in component space (metres) for rest data.
		TArray<ffg::Xform> RefCS;
		RefCS.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			const FTransform& T = Pose.GetRefPose(FCompactPoseBoneIndex(i));
			ffg::Xform L{FourfoldAnimRt::ToQ(T.GetRotation()), FourfoldAnimRt::ToV(T.GetTranslation(), FourfoldAnimRt::CmToM)};
			RefCS[i] = Parent[i] >= 0 ? L * RefCS[Parent[i]] : L;
		}
		if (Head != INDEX_NONE)
		{
			HeadFwdLocal = RefCS[Head].q.Inverse().Rotate(Frame.Axes.fwd).normalized();
		}
		for (int32 s = 0; s < 2; ++s)
		{
			if (Thigh[s] != INDEX_NONE && Calf[s] != INDEX_NONE && Foot[s] != INDEX_NONE)
			{
				LegLen[s] = (RefCS[Calf[s]].t - RefCS[Thigh[s]].t).length() + (RefCS[Foot[s]].t - RefCS[Calf[s]].t).length();
			}
		}
		// Secondary chains: ff_hair_01.., ff_sash_{l,r}_01.., ff_hem_{fl,fr,bl,br}_01..
		Chains.Reset();
		auto AddChain = [&](const FString& Prefix, const ffg::ChainParams& Params, bool bCollide) {
			FourfoldAnimRt::FChain C;
			for (int32 k = 1; k <= 9; ++k)
			{
				const int32 I = Find(*FString::Printf(TEXT("%s%02d"), *Prefix, k));
				if (I == INDEX_NONE)
				{
					break;
				}
				C.Bones.Add(I);
			}
			if (C.Bones.Num() == 0)
			{
				return;
			}
			const int32 Last = C.Bones.Last();
			const int32 Prev = Parent[Last];
			const ffg::Vec3 Seg = Prev >= 0 ? RefCS[Last].t - RefCS[Prev].t : ffg::Vec3(0.0f, 0.0f, -0.1f);
			const ffg::Vec3 TailCS = RefCS[Last].t + Seg;
			C.TailLocal = RefCS[Last].Inverse().Apply(TailCS);
			C.Sim.params = Params;
			C.bThighCollide = bCollide;
			Chains.Add(MoveTemp(C));
		};
		ffg::ChainParams Hair;
		Hair.stiffness = 2.4f;
		Hair.drag = 0.55f;
		Hair.gravity = 0.25f;
		Hair.radius = 0.015f;
		ffg::ChainParams Sash;
		Sash.stiffness = 0.9f;
		Sash.drag = 0.35f;
		Sash.gravity = 0.9f;
		Sash.radius = 0.015f;
		ffg::ChainParams Hem;
		Hem.stiffness = 1.6f;
		Hem.drag = 0.45f;
		Hem.gravity = 0.6f;
		Hem.radius = 0.02f;
		Hem.max_angle = 0.9f;
		AddChain(TEXT("ff_hair_"), Hair, false);
		AddChain(TEXT("ff_sash_l_"), Sash, true);
		AddChain(TEXT("ff_sash_r_"), Sash, true);
		for (const TCHAR* H : {TEXT("ff_hem_fl_"), TEXT("ff_hem_fr_"), TEXT("ff_hem_bl_"), TEXT("ff_hem_br_")})
		{
			AddChain(H, Hem, true);
		}
		LastClip.Reset();
		Inertial.Stop();
		Planter.Reset();
	}

	// ------------------------------------------------------------------------------------------------ procedural

	void RecomputeCS(int32 From)
	{
		const int32 N = Local.Num();
		CS.SetNum(N, EAllowShrinking::No);
		for (int32 j = FMath::Max(From, 0); j < N; ++j)
		{
			CS[j] = Parent[j] >= 0 ? Local[j] * CS[Parent[j]] : Local[j];
		}
	}

	/** Rotates `Bone` by Q in model space about its own head (descendants follow). */
	void RotateGlobal(int32 Bone, const ffg::Quat& Q)
	{
		if (Bone == INDEX_NONE)
		{
			return;
		}
		const ffg::Quat ParentQ = Parent[Bone] >= 0 ? CS[Parent[Bone]].q : ffg::Quat::Identity();
		const ffg::Quat NewCS = (Q * CS[Bone].q).Normalized();
		Local[Bone].q = (ParentQ.Inverse() * NewCS).Normalized();
		RecomputeCS(Bone);
	}

	/** Sets the model-space rotation of `Bone`. */
	void SetGlobalRotation(int32 Bone, const ffg::Quat& Q)
	{
		if (Bone == INDEX_NONE)
		{
			return;
		}
		const ffg::Quat ParentQ = Parent[Bone] >= 0 ? CS[Parent[Bone]].q : ffg::Quat::Identity();
		Local[Bone].q = (ParentQ.Inverse() * Q).Normalized();
		RecomputeCS(Bone);
	}

	void TranslateGlobal(int32 Bone, const ffg::Vec3& D)
	{
		if (Bone == INDEX_NONE || D.length_squared() < 1e-12f)
		{
			return;
		}
		const ffg::Quat ParentQ = Parent[Bone] >= 0 ? CS[Parent[Bone]].q : ffg::Quat::Identity();
		Local[Bone].t += ParentQ.Inverse().Rotate(D);
		RecomputeCS(Bone);
	}

	void Procedural()
	{
		const ffg::ModelAxes& Ax = Frame.Axes;
		const float Dt = Frame.Dt;
		RecomputeCS(0);
		const ffg::Vec3 Right = -Ax.left;
		const ffg::Vec3 AxPitch = Ax.up.cross(Ax.fwd);     // + tips the top forward
		const ffg::Vec3 AxRoll = Ax.up.cross(Right);       // + tips the top toward the character's right
		const ffg::Vec3 AxYaw = Ax.fwd.cross(Ax.left);     // + turns the front toward the character's left
		const FTransform& C2W = GetComponentTransform();
		ffg::Xform W;
		W.q = FourfoldAnimRt::ToQ(C2W.GetRotation());
		W.t = FourfoldAnimRt::ToV(C2W.GetTranslation(), FourfoldAnimRt::CmToM);

		// 1. animated foot goals (before any additive layer moves the hips)
		ffg::FootFrameOutput Fo;
		const bool bLegs = Thigh[0] != INDEX_NONE && Calf[0] != INDEX_NONE && Foot[0] != INDEX_NONE && Thigh[1] != INDEX_NONE &&
		                   Calf[1] != INDEX_NONE && Foot[1] != INDEX_NONE;
		const float IkW = bLegs ? FMath::Clamp(Frame.IkWeight, 0.0f, 1.0f) : 0.0f;
		static const ffg::ArenaGround NoArena;
		const bool bArena = Frame.Arena.IsValid() && Frame.Arena->valid;
		const FourfoldAnimRt::FArenaGroundSampler Sampler(bArena ? *Frame.Arena : NoArena);
		const ffg::GroundSampler* Ground = bArena ? &Sampler : nullptr;
		if (bLegs)
		{
			ffg::FootFrameInput In;
			In.dt = Dt;
			In.ik_w = IkW;
			In.lock_target_w = Frame.LockWeight;
			In.ground_speed = Frame.GroundSpeed;
			In.local_speed = Frame.LocalSpeed;
			In.model_lift = Frame.ModelLift;
			In.world_from_model = W;
			In.world_up = ffg::Vec3(0.0f, 0.0f, 1.0f);
			In.world_ref = ffg::Vec3(1.0f, 0.0f, 0.0f);
			for (int32 s = 0; s < 2; ++s)
			{
				In.feet[size_t(s)].ankle = CS[Foot[s]].t;
				In.feet[size_t(s)].foot_rot = CS[Foot[s]].q;
				In.feet[size_t(s)].ball = Ball[s] != INDEX_NONE ? CS[Ball[s]].t : CS[Foot[s]].t + Ax.fwd * 0.13f;
				In.feet[size_t(s)].plant_hint = Frame.PlantHint[s];
				In.thigh_heads[size_t(s)] = CS[Thigh[s]].t;
				In.leg_len[size_t(s)] = LegLen[s];
			}
			Planter.axes = Ax;
			Fo = Planter.Update(In, Ground);
		}

		// 2. pelvis: drop toward lower ground (IK) + landing compression
		const float Dy = Fo.pelvis_shift * IkW + Frame.LandY;
		TranslateGlobal(Pelvis, Ax.up * Dy);
		const float Pitch = Frame.LeanPitch - Frame.LandY * 0.9f;   // landing folds the chest forward a little
		const float Roll = Frame.LeanRoll;
		const ffg::Vec3 T = Frame.SpringTorso;
		const float HipShare = 0.35f * IkW;
		const float Tilt = Fo.hip_tilt * IkW;
		if (HipShare > 0.001f || FMath::Abs(Tilt) > 1e-4f)
		{
			RotateGlobal(Pelvis, ffg::Quat::AxisAngle(AxPitch, Pitch * HipShare) * ffg::Quat::AxisAngle(AxRoll, Roll * HipShare + Tilt) *
			                         ffg::Quat::FromRotVec(T, 0.25f * IkW));
		}
		// 3. spine and chest: lean, counter-tilt, aim, hit spring (spine_01-02 / spine_03-05 share the rotation)
		const float Rest = 1.0f - HipShare;
		const float TorsoRest = 1.0f - 0.25f * IkW;
		const float Aim = Frame.AimYaw * Frame.AimWeight;
		const ffg::Quat SpineQ = ffg::Quat::AxisAngle(AxYaw, Aim * 0.4f) * ffg::Quat::AxisAngle(AxPitch, Pitch * Rest * 0.5f) *
		                         ffg::Quat::AxisAngle(AxRoll, Roll * Rest * 0.5f - Tilt) * ffg::Quat::FromRotVec(T, TorsoRest * 0.45f);
		const ffg::Quat ChestQ = ffg::Quat::AxisAngle(AxYaw, Aim * 0.6f) * ffg::Quat::AxisAngle(AxPitch, Pitch * Rest * 0.5f) *
		                         ffg::Quat::AxisAngle(AxRoll, Roll * Rest * 0.5f) * ffg::Quat::FromRotVec(T, TorsoRest * 0.55f);
		ApplySplit(Spine, 0, 2, SpineQ);
		ApplySplit(Spine, 2, 5, ChestQ);
		// breathing: the chest rises and the clavicles lift a little (stances)
		if (Frame.Breathe > 0.01f)
		{
			const float B = FMath::Sin(Frame.BreathePhase) * Frame.Breathe;
			ApplySplit(Spine, 2, 5, ffg::Quat::AxisAngle(AxPitch, -0.012f * B));
			RotateGlobal(Clav[0], ffg::Quat::AxisAngle(Ax.fwd, 0.010f * B));
			RotateGlobal(Clav[1], ffg::Quat::AxisAngle(Ax.fwd, -0.010f * B));
		}
		// 4. arms: hit springs
		RotateGlobal(UpperArm[0], ffg::Quat::FromRotVec(Frame.SpringArmL));
		RotateGlobal(UpperArm[1], ffg::Quat::FromRotVec(Frame.SpringArmR));
		// 5. neck / head: a little more level than the banked torso, then the spring (whiplash)
		{
			const ffg::Quat NeckQ = ffg::Quat::AxisAngle(AxRoll, -Roll * 0.25f) * ffg::Quat::FromRotVec(Frame.SpringHead, 0.4f);
			ApplyNeck(NeckQ);
			RotateGlobal(Head, ffg::Quat::AxisAngle(AxRoll, -Roll * 0.25f) * ffg::Quat::FromRotVec(Frame.SpringHead, 0.6f));
		}
		// 6. look-at (clamped, smoothed): chest 20 % / neck 35 % / head 45 % of the yaw; neck 40 % / head 60 % of the pitch
		if (Head != INDEX_NONE)
		{
			Look.axes = Ax;
			const ffg::Vec3 HeadFwd = CS[Head].q.Rotate(HeadFwdLocal);
			Look.Update(Dt, CS[Head].t, HeadFwd, Frame.LookTarget, Frame.bHasLook ? Frame.LookWeight : 0.0f);
			if (FMath::Abs(Look.yaw) > 1e-4f || FMath::Abs(Look.pitch) > 1e-4f)
			{
				RotateGlobal(Spine[4], Look.Rotation(0.2f, 0.0f, 0.0f));
				ApplyNeck(Look.Rotation(0.35f, 0.4f, 0.55f));
				RotateGlobal(Head, Look.Rotation(0.45f, 0.6f, 1.0f));
			}
		}
		// 7. foot IK
		if (bLegs && Fo.use_ik && IkW > 0.002f)
		{
			for (int32 s = 0; s < 2; ++s)
			{
				const ffg::TwoBoneResult R =
					ffg::SolveTwoBone(CS[Thigh[s]].t, CS[Calf[s]].t, CS[Foot[s]].t, CS[Thigh[s]].q, CS[Calf[s]].q, CS[Foot[s]].q,
					                  Fo.goal[size_t(s)], Fo.goal_rot[size_t(s)], IkW, Ax.left);
				SetGlobalRotation(Thigh[s], R.thigh);
				SetGlobalRotation(Calf[s], R.calf);
				SetGlobalRotation(Foot[s], R.foot);
			}
		}
		// 8. secondary spring chains (world space, so the cloth lags the body's motion)
		if (Frame.bChains && Frame.Lod == 0 && Chains.Num() > 0)
		{
			SimulateChains(W, Dt);
		}
	}

	/** Applies Q over bones [From, To) of `Bones`, an equal share to each one that exists. */
	void ApplySplit(const int32* Bones, int32 From, int32 To, const ffg::Quat& Q)
	{
		int32 Count = 0;
		for (int32 k = From; k < To; ++k)
		{
			Count += Bones[k] != INDEX_NONE ? 1 : 0;
		}
		if (Count == 0)
		{
			return;
		}
		const ffg::Quat Part = ffg::Slerp(ffg::Quat::Identity(), Q, 1.0f / float(Count));
		for (int32 k = From; k < To; ++k)
		{
			RotateGlobal(Bones[k], Part);
		}
	}

	void ApplyNeck(const ffg::Quat& Q)
	{
		ApplySplit(Neck, 0, 2, Q);
	}

	void SimulateChains(const ffg::Xform& W, float Dt)
	{
		std::vector<ffg::CapsuleCollider> Caps;
		for (int32 s = 0; s < 2; ++s)
		{
			if (Thigh[s] != INDEX_NONE && Calf[s] != INDEX_NONE)
			{
				ffg::CapsuleCollider C;
				C.a = W.Apply(CS[Thigh[s]].t);
				C.b = W.Apply(CS[Calf[s]].t);
				C.radius = 0.065f;
				Caps.push_back(C);
			}
		}
		const std::vector<ffg::CapsuleCollider> NoCaps;
		std::vector<ffg::Vec3> Pts;
		std::vector<ffg::Vec3> Dirs;
		const ffg::Quat Winv = W.q.Inverse();
		for (FourfoldAnimRt::FChain& Ch : Chains)
		{
			const int32 Num = Ch.Bones.Num();
			Pts.clear();
			for (int32 k = 0; k < Num; ++k)
			{
				Pts.push_back(W.Apply(CS[Ch.Bones[k]].t));
			}
			Pts.push_back(W.Apply(CS[Ch.Bones.Last()].Apply(Ch.TailLocal)));
			Ch.Sim.gravity_dir = ffg::Vec3(0.0f, 0.0f, -1.0f);
			Ch.Sim.Step(Dt, Pts, Ch.bThighCollide ? Caps : NoCaps, Dirs);
			for (int32 k = 0; k < Num && size_t(k) < Dirs.size(); ++k)
			{
				const int32 B = Ch.Bones[k];
				// current direction of this bone (to the next chain bone, or its virtual tail), model space
				const ffg::Vec3 Next = k + 1 < Num ? CS[Ch.Bones[k + 1]].t : CS[B].Apply(Ch.TailLocal);
				const ffg::Vec3 Cur = Next - CS[B].t;
				const ffg::Vec3 Want = Winv.Rotate(Dirs[size_t(k)]);
				if (Cur.length_squared() > 1e-10f && Want.length_squared() > 1e-10f)
				{
					RotateGlobal(B, ffg::Quat::FromTo(Cur, Want));
				}
			}
		}
	}
};

void UFourfoldAnimInstance::SetFrame(const FFourfoldAnimFrame& InFrame)
{
	const bool bReset = InFrame.bReset || (bHasPending && Pending.bReset);
	Pending = InFrame;
	Pending.bReset = bReset;
	bHasPending = true;
}

FAnimInstanceProxy* UFourfoldAnimInstance::CreateAnimInstanceProxy()
{
	return new FFourfoldAnimProxy(this);
}

void UFourfoldAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
