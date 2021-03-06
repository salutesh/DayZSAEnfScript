/**@class		AbilityRecord
 * @brief		pair ( action, actionType )
 **/
class AbilityRecord
{
	int m_action; /// corresponds to Human::actions == RELOAD, MECHANISM, ...
	int m_actionType; /// corresponds to Human::actionTypes == CHAMBERING_ONEBULLET_CLOSED, MECHANISM_CLOSED...
	void AbilityRecord (int a, int at) { m_action = a; m_actionType = at; }
};

typedef FSMTransition<WeaponStateBase, WeaponEventBase, WeaponActionBase, WeaponGuardBase> WeaponTransition; /// shorthand

/**@class		Weapon_Base
 * @brief		script base for all weapons
 *
 * @NOTE: this class is bound to core-config "Weapon_Base" config class
 **/
class Weapon_Base extends Weapon
{
	protected ref array<ref AbilityRecord> m_abilities = new array<ref AbilityRecord>;		/// weapon abilities
	protected ref WeaponFSM m_fsm;	/// weapon state machine
	protected bool m_isJammed = false;
	protected bool m_LiftWeapon = false;
	protected bool m_BayonetAttached;
	protected bool m_ButtstockAttached;
	protected int m_weaponAnimState = -1; /// animation state the weapon is in, -1 == uninitialized
	protected int m_weaponHideBarrelIdx = -1; //index in simpleHiddenSelections cfg array
	protected float m_DmgPerShot;
	protected float m_WeaponLength;
	ref array<float> m_DOFProperties = new array<float>;
	ref array<float> m_ChanceToJam = new array<float>;
	protected float m_ChanceToJamSync = 0;
	protected ref PropertyModifiers m_PropertyModifierObject;
	protected PhxInteractionLayers hit_mask = PhxInteractionLayers.CHARACTER | PhxInteractionLayers.BUILDING | PhxInteractionLayers.DOOR | PhxInteractionLayers.VEHICLE | PhxInteractionLayers.ROADWAY | PhxInteractionLayers.TERRAIN | PhxInteractionLayers.ITEM_SMALL | PhxInteractionLayers.ITEM_LARGE | PhxInteractionLayers.FENCE | PhxInteractionLayers.AI;

	void Weapon_Base ()
	{
		m_DmgPerShot 		= ConfigGetFloat("damagePerShot");
		m_BayonetAttached 	= false;
		m_ButtstockAttached = false;
		
		if ( ConfigIsExisting("simpleHiddenSelections") )
		{
			TStringArray selectionNames = new TStringArray;
			ConfigGetTextArray("simpleHiddenSelections",selectionNames);
			m_weaponHideBarrelIdx = selectionNames.Find("hide_barrel");
		}
		
		InitWeaponLength();
		InitDOFProperties(m_DOFProperties);
		if(GetGame().IsServer())
		{
			InitReliability(m_ChanceToJam);
		}
		InitStateMachine();
	}

	void InitStateMachine () { }

	bool CanProcessAction (int action, int actionType)
	{
		return false; // @TODO
	}
	/**@fn		HasActionAbility
	 * @brief	query if weapon supports action and actionType
	 * @param[in]	action	\p	one of Human.actions (i.e. RELOAD, MECHANISM, ...)
	 * @param[in]	actionType	\p	one of Human.actionTypes (i.e. CHAMBERING_ONEBULLET_CLOSED, MECHANISM_CLOSED...)
	 * @return	true if weapon supports operation
	 **/
	bool HasActionAbility (int action, int actionType)
	{
		int count = GetAbilityCount();
		for (int i = 0; i < count; ++i)
		{
			AbilityRecord rec = GetAbility(i);
			if (rec.m_action == action && rec.m_actionType == actionType)
				return true;
		}
		return false;
	}
	/**@fn		GetAbilityCount
	 * @return number of stored abilities
	 **/
	int GetAbilityCount () { return m_abilities.Count(); }
	/**@fn		GetAbility
	 * @param[in]	index	\p	index into m_abilities storage
	 * @return ability record
	 **/
	AbilityRecord GetAbility (int index) { return m_abilities.Get(index); }

	/**@fn		CanProcessWeaponEvents
	 * @return	true if weapon has running fsm
	 **/
	bool CanProcessWeaponEvents () { return m_fsm && m_fsm.IsRunning(); }

	/**@fn		GetCurrentState
	 * @brief		returns currently active state
	 * @return	current state the FSM is in (or NULL)
	 **/
	WeaponStateBase GetCurrentState () { return m_fsm.GetCurrentState(); }

	/**@fn		IsWaitingForActionFinish
	 * @brief	returns true if state machine started playing action/actionType and waits for finish
	 **/
	bool IsWaitingForActionFinish ()
	{
		return CanProcessWeaponEvents() && GetCurrentState().IsWaitingForActionFinish();
	}

	bool IsIdle ()
	{
		return CanProcessWeaponEvents() && GetCurrentState().IsIdle();
	}

	/**@fn	ProcessWeaponEvent
	 * @brief	weapon's fsm handling of events
	 * @NOTE: warning: ProcessWeaponEvent can be called only within DayZPlayer::HandleWeapons (or ::CommandHandler)
	 **/
	bool ProcessWeaponEvent (WeaponEventBase e)
	{
		SyncEventToRemote(e);
		
		// @NOTE: synchronous events not handled by fsm
		if (e.GetEventID() == WeaponEventID.SET_NEXT_MUZZLE_MODE)
		{
			SetNextMuzzleMode(GetCurrentMuzzle());
			return true;
		}

		if (m_fsm.ProcessEvent(e) == ProcessEventResult.FSM_OK)
			return true;

		//wpnDebugPrint("FSM refused to process event (no transition): src=" + GetCurrentState().ToString() + " event=" + e.ToString());
		return false;
	}
	/**@fn	ProcessWeaponAbortEvent
	 * @NOTE: warning: ProcessWeaponEvent can be called only within DayZPlayer::HandleWeapons (or ::CommandHandler)
	 **/
	bool ProcessWeaponAbortEvent (WeaponEventBase e)
	{
		SyncEventToRemote(e);
		
		ProcessEventResult aa;
		m_fsm.ProcessAbortEvent(e, aa);
		return aa == ProcessEventResult.FSM_OK;
	}

	bool CanChamberBullet (int muzzleIndex, Magazine mag)
	{
		return CanChamberFromMag(muzzleIndex, mag) && (!IsChamberFull(muzzleIndex) || IsChamberFiredOut(muzzleIndex) || !IsInternalMagazineFull(muzzleIndex)) );
	}

	void SetWeaponAnimState (int state)
	{
		m_weaponAnimState = state;
	}
	void ResetWeaponAnimState ()
	{
		fsmDebugSpam("[wpnfsm] resetting anim state: " + typename.EnumToString(PistolAnimState, m_weaponAnimState) + " --> " + typename.EnumToString(PistolAnimState, -1));
		m_weaponAnimState = -1;
	}
	int GetWeaponAnimState () { return m_weaponAnimState; }

	void EEFired (int muzzleType, int mode, string ammoType)
	{
		ItemBase suppressor = GetAttachedSuppressor();
		if ( !GetGame().IsServer()  ||  !GetGame().IsMultiplayer() )
		{
			// Muzzle flash & overheating effects
			ItemBase.PlayFireParticles(this, ammoType, this, suppressor, "CfgWeapons" );
			IncreaseOverheating(this, ammoType, this, suppressor, "CfgWeapons");
			
			if (suppressor)
			{
				ItemBase.PlayFireParticles(this, ammoType, suppressor, NULL, "CfgVehicles" );
				suppressor.IncreaseOverheating(this, ammoType, this, suppressor, "CfgVehicles");
			}
		}
		
		if (GetGame().IsServer())
		{
			AddHealth("","Health",-m_DmgPerShot); //damages weapon
			if (suppressor)
				suppressor.AddHealth("","Health",-m_DmgPerShot); //damages suppressor; TODO add suppressor damage coeficient/parameter (?) to suppressors/weapons (?)
		}
		//JamCheck(muzzleType);
		
		#ifdef DEVELOPER
		MiscGameplayFunctions.UnlimitedAmmoDebugCheck(this);
		#endif
	}
	
	bool JamCheck (int muzzleIndex )
	{
		PlayerBase player = PlayerBase.Cast(GetHierarchyRootPlayer());
		if ( player )
		{
			float rnd = player.GetRandomGeneratorSyncManager().GetRandom01(RandomGeneratorSyncUsage.RGSJam);
			Print("Random Jam - " + rnd);
			if (rnd < GetSyncChanceToJam())
				return true;
		}
		return false;
	}
	
	bool IsJammed () { return m_isJammed; }
	void SetJammed (bool value) { m_isJammed = value; }
	float GetSyncChanceToJam () { return m_ChanceToJamSync; }
	float GetChanceToJam()
	{
		int level = GetHealthLevel();
	
		if (level >= 0 && level < m_ChanceToJam.Count())
			return m_ChanceToJam[level];
		else
			return 0.0;
	}
	
	void SyncSelectionState (bool has_bullet, bool has_mag)
	{
		if (has_bullet)
			SelectionBulletShow();
		else
			SelectionBulletHide();

		if (has_mag)
			SelectionMagazineShow();
		else
			SelectionMagazineHide();
	}

	override bool OnStoreLoad (ParamsReadContext ctx, int version)
	{
		if ( !super.OnStoreLoad(ctx, version) )
			return false;
		
		if (version >= 105)
		{
			int mode_count = 0;
			if (!ctx.Read(mode_count))
				Error("Weapon.OnStoreLoad " + this + " cannot read mode count!");

			for (int m = 0; m < mode_count; ++m)
			{
				int mode = 0;
				if (!ctx.Read(mode))
					Error("Weapon.OnStoreLoad " + this + " cannot read mode[" + m + "]");
				
				wpnDebugPrint("[wpnfsm] OnStoreLoad - loaded muzzle[" + m + "].mode = " + mode);
				SetCurrentMode(m, mode);
			}
		}

		if (m_fsm)
		{
			m_fsm.OnStoreLoad(ctx, version);
		}
		else
		{
			int dummy = 0;
			ctx.Read(dummy);
		}

		return true;
	}

	void SaveCurrentFSMState (ParamsWriteContext ctx)
	{
		if (m_fsm && m_fsm.IsRunning())
		{
			if (m_fsm.SaveCurrentFSMState(ctx))
				wpnDebugPrint("[wpnfsm] Weapon=" + this + " state saved.");
			else
				Error("[wpnfsm] Weapon=" + this + " state NOT saved.");
		}
		else
			Error("[wpnfsm] Weapon.SaveCurrentFSMState: trying to save weapon without FSM (or uninitialized weapon) this=" + this + " type=" + GetType());
	}

	bool LoadCurrentFSMState (ParamsReadContext ctx, int version)
	{
		if (m_fsm)
		{
			if (m_fsm.LoadCurrentFSMState(ctx, version))
			{
				WeaponStableState state = WeaponStableState.Cast(GetCurrentState());
				if (state)
				{
					SyncSelectionState(state.HasBullet(), state.HasMagazine());
					state.SyncAnimState();
					wpnDebugPrint("[wpnfsm] Weapon=" + this + " stable state loaded and synced.");
					return true;
				}
				else
				{
					wpnDebugPrint("[wpnfsm] Weapon=" + this + " unstable/error state loaded.");
					return false;
				}
			}
			else
			{
				Error("[wpnfsm] Weapon=" + this + " did not load.");
				return false;
			}
		}
		else
		{
			Error("[wpnfsm] Weapon.LoadCurrentFSMState: trying to load weapon without FSM (or uninitialized weapon) this=" + this + " type=" + GetType());
			return false;
		}
	}

	override void AfterStoreLoad ()
	{
		if (m_fsm)
		{
			int mi = GetCurrentMuzzle();
			Magazine mag = GetMagazine(mi);
			bool has_mag = mag != null;
			bool has_bullet = !IsChamberEmpty(mi);
			SyncSelectionState(has_bullet, has_mag);
		}
	}

	override void OnStoreSave (ParamsWriteContext ctx)
	{
		super.OnStoreSave(ctx);
		
		// fire mode added in version 105
		int mode_count = GetMuzzleCount();
		ctx.Write(mode_count);
		for (int m = 0; m < mode_count; ++m)
			ctx.Write(GetCurrentMode(m));

		if (m_fsm)
			m_fsm.OnStoreSave(ctx);
		else
		{
			int dummy = 0;
			ctx.Write(dummy);
		}
	}

	/**@fn		GetCurrentStateID
	 * @brief	tries to return identifier of current state
	 **/
	int GetInternalStateID ()
	{
		if (m_fsm)
			return m_fsm.GetInternalStateID();
		return 0;
	}
	
	/**@fn		GetCurrentStableStateID
	 * @brief	tries to return identifier of current stable state (or nearest stable state if unstable state is currently running)
	 **/
	int GetCurrentStableStateID ()
	{
		if (m_fsm)
			return m_fsm.GetCurrentStableStateID();
		return 0;
	}

	/**@fn		RandomizeFSMState
	 * @brief	Engine callback - loot randomization of FSM's state. not intended to direct use.
	 **/
	protected void RandomizeFSMState ()
	{
		if (m_fsm)
		{
			int mi = GetCurrentMuzzle();
			Magazine mag = GetMagazine(mi);
			bool has_mag = mag != null;
			bool has_bullet = !IsChamberEmpty(mi);
			bool has_jam = IsJammed();
			m_fsm.RandomizeFSMState(has_bullet, has_mag, has_jam);
			SyncSelectionState(has_bullet, has_mag);
		}
	}

	/**
	 * @brief	Load posible actions for wepon from config
	 **/
	override void SetUserActions ()
	{
		g_Game.ConfigGetIntArray("cfgWeapons " + GetType() + " ContinuousActions", m_ContinuousActions);
		g_Game.ConfigGetIntArray("cfgWeapons " + GetType() + " SingleUseActions", m_SingleUseActions);
		g_Game.ConfigGetIntArray("cfgWeapons " + GetType() + " InteractActions", m_InteractActions);
	}
	/**
	 * @brief Returns number of slots for attachments corrected for weapons
	 **/
	override int GetSlotsCountCorrect ()
	{
		int ac = GetInventory().AttachmentCount();
		int	sc = GetInventory().GetAttachmentSlotsCount() + GetMuzzleCount();
		if (ac > sc) sc = ac; // fix of some weapons which has 1 attachments but 0 slots...
		return sc;
	};

	PropertyModifiers GetPropertyModifierObject () { return m_PropertyModifierObject; }

	bool CreatePropertyModifierObject ()
	{
		if (!m_PropertyModifierObject)
		{
			m_PropertyModifierObject = new PropertyModifiers(this);
			return true;
		}
		return false;
	}

	bool RemovePropertyModifierObject ()
	{
		if (m_PropertyModifierObject)
		{
			m_PropertyModifierObject = null;
			return true;
		}
		return false;
	}
	
	override void OnInventoryEnter (Man player)
	{
		super.OnInventoryEnter(player);

		CreatePropertyModifierObject();
	}
	
	override void OnInventoryExit (Man player)
	{
		super.OnInventoryExit(player);

		RemovePropertyModifierObject();
	}

	override void EEItemAttached (EntityAI item, string slot_name)
	{
		super.EEItemAttached(item, slot_name);

		if ( GetPropertyModifierObject() ) 	
			GetPropertyModifierObject().UpdateModifiers();
	}

	override void EEItemDetached (EntityAI item, string slot_name)
	{
		super.EEItemDetached(item, slot_name);

		if ( GetPropertyModifierObject() ) 	
			GetPropertyModifierObject().UpdateModifiers();
	}
	
	override void OnItemLocationChanged(EntityAI old_owner, EntityAI new_owner)
	{
		super.OnItemLocationChanged(old_owner,new_owner);
		
		// HACK "resets" optics memory on optics
		PlayerBase player;
		if ( PlayerBase.CastTo(player,old_owner) )
		{ 
			player.SetReturnToOptics(false);
		}
		HideWeaponBarrel(false);
	}
	
	override bool CanReleaseAttachment(EntityAI attachment)
	{
		PlayerBase player = PlayerBase.Cast( GetHierarchyRootPlayer() );
		if( player )
		{
			if( player.GetItemInHands() == this )
				return true;
		}
		return false;
	}

	bool IsRemoteWeapon ()
	{
		InventoryLocation il = new InventoryLocation;
		if (GetInventory().GetCurrentInventoryLocation(il))
		{
			EntityAI parent = il.GetParent();
			DayZPlayer dayzp = DayZPlayer.Cast(parent);
			if (il.GetType() == InventoryLocationType.HANDS && dayzp)
			{
				bool remote = dayzp.GetInstanceType() == DayZPlayerInstanceType.INSTANCETYPE_REMOTE;
				return remote;
			}
		}
		return true;
	}

	void SyncEventToRemote (WeaponEventBase e)
	{
		DayZPlayer p = DayZPlayer.Cast(GetHierarchyParent());
		if (p && p.GetInstanceType() == DayZPlayerInstanceType.INSTANCETYPE_SERVER)
		{
			ScriptRemoteInputUserData ctx = new ScriptRemoteInputUserData;

			ctx.Write(INPUT_UDT_WEAPON_REMOTE_EVENT);
			e.WriteToContext(ctx);

			wpnDebugPrint("[wpnfsm] send 2 remote: sending e=" + e + " id=" + e.GetEventID() + " p=" + e.m_player + "  m=" + e.m_magazine);
			p.StoreInputForRemotes(ctx);
		}
	}

	
	RecoilBase SpawnRecoilObject ()
	{
		return new DefaultRecoil(this);
	}

	int GetWeaponSpecificCommand (int weaponAction, int subCommand) { return subCommand; }

	bool CanFire ()
	{
		if (!IsChamberEmpty(GetCurrentMuzzle()) && !IsChamberFiredOut(GetCurrentMuzzle()) && !IsJammed() && !m_LiftWeapon)
			return true;
		return false;
	}
	
	override int GetItemWeight ()
	{
		int AttachmentWeight = 0;
		float item_wetness = GetWet();
		int AttachmentsCount = GetInventory().AttachmentCount();
		int ConfWeight = ConfigGetInt("weight");
		
		for (int i = 0; i < AttachmentsCount; i++)
		{
			AttachmentWeight += GetInventory().GetAttachmentFromIndex(i).GetItemWeight();
		}
		
		return Math.Round((item_wetness + 1) * (ConfWeight + AttachmentWeight));
	}
			
	bool CanEnterIronsights ()
	{
		ItemOptics optic = GetAttachedOptics();
		if( !optic )
			return true;
		
		return optic.HasWeaponIronsightsOverride();
	}
	
	//! Initializes DOF properties for weapon's ironsight/optics cameras
	bool InitDOFProperties (out array<float> temp_array)
	{
		if (GetGame().ConfigIsExisting("cfgWeapons " + GetType() + " PPDOFProperties"))
		{
			GetGame().ConfigGetFloatArray("cfgWeapons " + GetType() + " PPDOFProperties", temp_array);
			return true;
		}
		return false;
	}
	
	bool InitReliability (out array<float> reliability_array)
	{
		if (GetGame().ConfigIsExisting("cfgWeapons " + GetType() + " Reliability ChanceToJam"))
		{
			GetGame().ConfigGetFloatArray("cfgWeapons " + GetType() + " Reliability ChanceToJam", reliability_array);
			return true;
		}
		return false;
	}
	
	//!gets weapon length from config for weaponlift raycast
	bool InitWeaponLength()
	{
		if (ConfigIsExisting("WeaponLength"))
		{
			m_WeaponLength = ConfigGetFloat("WeaponLength");
			return true;
		}
		m_WeaponLength = 0;
		return false;
	}
	
	ref array<float> GetWeaponDOF ()
	{
		return m_DOFProperties;
	}
	
	// lifting weapon on obstcles
	bool LiftWeaponCheck (PlayerBase player)
	{
		int idx;
		float distance;
		float hit_fraction; //junk
		vector start, end;
		vector direction;
		vector usti_hlavne_position;
		vector trigger_axis_position;
		vector hit_pos, hit_normal; //junk
		Object obj;
		ItemBase attachment;
		
		m_LiftWeapon = false;
		// not a gun, no weap.raise for now
		if ( HasSelection("Usti hlavne") )
			return false;
		
		if (!player)
		{
			Print("Error: No weapon owner, returning");
			return false;
		}
		
		// weapon not raised
		if ( player.GetInputController() && !player.GetInputController().IsWeaponRaised() )
			return false;
		
		usti_hlavne_position = GetSelectionPositionLS( "Usti hlavne" ); 	// Usti hlavne
		//vector weapon_back_position = GetSelectionPosition( "Weapon back" ); 	// back of weapon; barrel length
		trigger_axis_position = GetSelectionPositionLS("trigger_axis");
		//usti_hlavne_position = ModelToWorld(usti_hlavne_position);
		
		// freelook raycast
		if (player.GetInputController().CameraIsFreeLook())
		{
			if (player.m_DirectionToCursor != vector.Zero)
			{
				direction = player.m_DirectionToCursor;
			}
			// if player raises weapon in freelook
			else
			{
				direction = MiscGameplayFunctions.GetHeadingVector(player);
			}
		}
		else
		{
			direction = GetGame().GetCurrentCameraDirection(); // exception for freelook. Much better this way!
		}
		
		idx = player.GetBoneIndexByName("Neck"); //RightHandIndex1
		if ( idx == -1 )
			{ start = player.GetPosition()[1] + 1.5; }
		else
			{ start = player.GetBonePositionWS(idx); }
		
		//distance = vector.Distance(usti_hlavne_position,trigger_axis_position) + 0.1;
		//! snippet below measures distance from "RightHandIndex1" bone for lifting calibration
		/*usti_hlavne_position = ModelToWorld(usti_hlavne_position);
		distance = vector.Distance(start,usti_hlavne_position);*/
		distance = m_WeaponLength;// - 0.05; //adjusted raycast length

		// if weapon has battel attachment, does longer cast
		if (ItemBase.CastTo(attachment,FindAttachmentBySlotName("weaponBayonet")) || ItemBase.CastTo(attachment,FindAttachmentBySlotName("weaponBayonetAK")) || ItemBase.CastTo(attachment,FindAttachmentBySlotName("weaponBayonetMosin")) || ItemBase.CastTo(attachment,FindAttachmentBySlotName("weaponBayonetSKS")) || ItemBase.CastTo(attachment,GetAttachedSuppressor()))
		{
			distance += attachment.m_ItemModelLength;
			/*
			vector mins, maxs;
			//attachment.GetWorldBounds(mins, maxs);
			mins = attachment.GetMemoryPointPos(BoundingBox_min)
			maxs = attachment.GetMemoryPointPos(BoundingBox_max)
			Print(vector.Distance(mins, maxs));
			Print("mins " + mins);
			Print("maxs " + maxs);
			distance += 0.2; 
			*/
		}
		end = start + (direction * distance);
		DayZPhysics.RayCastBullet(start, end, hit_mask, player, obj, hit_pos, hit_normal, hit_fraction);
		
		// something is hit
		if (hit_pos != vector.Zero)
		{
			m_LiftWeapon = true;
			return true;
		}
		return false;
	}
	
	void SetSyncJammingChance( float jamming_chance )
	{
		m_ChanceToJamSync = jamming_chance;
	}
	
	/**@fn		EjectCartridge
	 * @brief	unload bullet from chamber or internal magazine
	 *
	 * @NOTE: 	EjectCartridge = GetCartridgeInfo + PopCartridge
	 *
	 * @param[in] muzzleIndex
	 * @param[out] ammoDamage \p  damage of the ammo
	 * @param[out] ammoTypeName \p	 type name of the ejected ammo
	 * @return	true if bullet removed from chamber
	 **/
	bool EjectCartridge (int muzzleIndex, out float ammoDamage, out string ammoTypeName)
	{
		if (IsChamberEjectable(muzzleIndex))
		{
			if (PopCartridgeFromChamber(muzzleIndex, ammoDamage, ammoTypeName))
				return true;
		}
		else if (GetInternalMagazineCartridgeCount(muzzleIndex) > 0)
		{
			if (PopCartridgeFromInternalMagazine(muzzleIndex, ammoDamage, ammoTypeName))
				return true;
		}
		return false;
	}
	
	bool CopyWeaponStateFrom (notnull Weapon_Base src)
	{
		float damage = 0.0;
		string type;

		for (int mi = 0; mi < src.GetMuzzleCount(); ++mi)
		{
			if (!src.IsChamberEmpty(mi))
			{
				if (src.GetCartridgeInfo(mi, damage, type))
				{
					PushCartridgeToChamber(mi, damage, type);
				}
			}
			
			for (int ci = 0; ci < src.GetInternalMagazineCartridgeCount(mi); ++ci)
			{
				if (src.GetInternalMagazineCartridgeInfo(mi, ci, damage, type))
				{
					PushCartridgeToInternalMagazine(mi, damage, type);
				}
			}
		}
		
		int dummy_version = int.MAX;
		ScriptReadWriteContext ctx = new ScriptReadWriteContext;
		src.OnStoreSave(ctx.GetWriteContext());
		OnStoreLoad(ctx.GetReadContext(), dummy_version);
		return true;
	}
	
	//! attachment helpers (firearm melee)
	override void SetBayonetAttached(bool pState)
	{
		m_BayonetAttached = pState;
	}
	
	override bool HasBayonetAttached()
	{
		return m_BayonetAttached;
	}
	
	override void SetButtstockAttached(bool pState)
	{
		m_ButtstockAttached = pState;
	}

	override bool HasButtstockAttached()
	{
		return m_ButtstockAttached;
	}
	
	void HideWeaponBarrel(bool state)
	{
		if ( !GetGame().IsMultiplayer() || GetGame().IsClient() )//hidden for client only
		{
			ItemOptics optics = GetAttachedOptics();
			if ( optics && !optics.AllowsDOF() && m_weaponHideBarrelIdx != -1 )
			{
				SetSimpleHiddenSelectionState(m_weaponHideBarrelIdx,!state);
			}
		}
	}

	override void SetActions()
	{
		super.SetActions();
		AddAction(FirearmActionAttachMagazine);
		AddAction(FirearmActionLoadBullet);
		AddAction(ActionTurnOnWeaponFlashlight);
		AddAction(ActionTurnOffWeaponFlashlight);
	}
};

