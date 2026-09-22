
    case fpcNm_Obj_Ychndlr_e:
        out[0] = {offsetof(daObjYchndlr_c, field_0x5a8), offsetof(daObjYchndlr_c, mpChandlierModel)};
        out[1] = {offsetof(daObjYchndlr_c, mShaftRotZ), offsetof(daObjYchndlr_c, mCcStts)};
        out[2] = {offsetof(daObjYchndlr_c, field_0xc8c), offsetof(daObjYchndlr_c, mChandelierCenter) + sizeof(daObjYchndlr_c::mChandelierCenter)};
        return 3;
    case fpcNm_Obj_Lv4Chan_e:
        out[0] = {offsetof(daObjLv4Chan_c, field_0x5a8), offsetof(daObjLv4Chan_c, mModel)};
        out[1] = {offsetof(daObjLv4Chan_c, mChainLengths), offsetof(daObjLv4Chan_c, mChains)};
        out[2] = {offsetof(daObjLv4Chan_c, mMdlLengths), offsetof(daObjLv4Chan_c, mMdls)};
        out[3] = {offsetof(daObjLv4Chan_c, field_0x23bc), offsetof(daObjLv4Chan_c, field_0x23d8)};
        out[4] = {offsetof(daObjLv4Chan_c, field_0x2418), offsetof(daObjLv4Chan_c, mShadowId)};
        out[5] = {offsetof(daObjLv4Chan_c, field_0x247c), offsetof(daObjLv4Chan_c, field_0x24a0) + sizeof(daObjLv4Chan_c::field_0x24a0)};
        return 6;
    case fpcNm_Obj_Chandelier_e:
        out[0] = {offsetof(daObjChandelier_c, field_0x5a8), offsetof(daObjChandelier_c, mpModel)};
        out[1] = {offsetof(daObjChandelier_c, field_0x5ec), offsetof(daObjChandelier_c, field_0x60b) + sizeof(daObjChandelier_c::field_0x60b)};
        return 2;
    case fpcNm_Obj_Lv6Tenbin_e:
        out[0] = {offsetof(daTenbin_c, mMode), offsetof(daTenbin_c, mdBgW)};
        out[1] = {offsetof(daTenbin_c, field_0x620), offsetof(daTenbin_c, field_0x650) + sizeof(daTenbin_c::field_0x650)};
        return 2;
    case fpcNm_Obj_Lv6ElevtA_e:
        out[0] = {offsetof(daObjLv6ElevtA_c, mMtx1), offsetof(daObjLv6ElevtA_c, mModel)};
        out[1] = {offsetof(daObjLv6ElevtA_c, mAngle), offsetof(daObjLv6ElevtA_c, mIsMoving) + sizeof(daObjLv6ElevtA_c::mIsMoving)};
        return 2;
    case fpcNm_Obj_Lv8Lift_e:
        out[0] = {offsetof(daL8Lift_c, field_0x5c4), offsetof(daL8Lift_c, mPathID)};
        out[1] = {offsetof(daL8Lift_c, mPathDirection), offsetof(daL8Lift_c, mAcchCir)};
        out[2] = {offsetof(daL8Lift_c, field_0x804), offsetof(daL8Lift_c, mLightSet)};
        out[3] = {offsetof(daL8Lift_c, field_0x80e), offsetof(daL8Lift_c, mLight)};
        return 4;
    case fpcNm_Obj_Lv8OptiLift_e:
        out[0] = {offsetof(daOptiLift_c, mSwBit), offsetof(daOptiLift_c, mPathID)};
        out[1] = {offsetof(daOptiLift_c, mPathDirection), offsetof(daOptiLift_c, mLight)};
        out[2] = {offsetof(daOptiLift_c, mNoRideOffSwTimer), offsetof(daOptiLift_c, field_0x616) + sizeof(daOptiLift_c::field_0x616)};
        return 3;
    case fpcNm_Obj_Elevator_e:
        out[0] = {offsetof(daObjDmElevator_c, field_0x5D8), offsetof(daObjDmElevator_c, mpBgW)};
        out[1] = {offsetof(daObjDmElevator_c, mMtx), offsetof(daObjDmElevator_c, field_0x640) + sizeof(daObjDmElevator_c::field_0x640)};
        return 2;
    case fpcNm_Obj_PRElvtr_e:
        out[0] = {offsetof(daObjPRElvtr_c, mMtx1), offsetof(daObjPRElvtr_c, mpModel)};
        out[1] = {offsetof(daObjPRElvtr_c, field_0x60c), offsetof(daObjPRElvtr_c, field_0x60c) + sizeof(daObjPRElvtr_c::field_0x60c)};
        return 2;
    case fpcNm_Obj_SpinLift_e:
        out[0] = {offsetof(daSpinLift_c, field_0x5ac), offsetof(daSpinLift_c, field_0x5bc) + sizeof(daSpinLift_c::field_0x5bc)};
        return 1;
    case fpcNm_Obj_TakaraDai_e:
        out[0] = {offsetof(daTkraDai_c, mMode), offsetof(daTkraDai_c, mpBgW)};
        out[1] = {offsetof(daTkraDai_c, field_0x62c), offsetof(daTkraDai_c, field_0x62c) + sizeof(daTkraDai_c::field_0x62c)};
        return 2;
    case fpcNm_Obj_KWheel00_e:
        out[0] = {offsetof(daObjKWheel00_c, mNewBgMtx), offsetof(daObjKWheel00_c, mpModel)};
        out[1] = {offsetof(daObjKWheel00_c, mZAngularVelocity), offsetof(daObjKWheel00_c, m_type)};
        return 2;
    case fpcNm_Obj_KWheel01_e:
        out[0] = {offsetof(daObjKWheel01_c, mNewBgMtx), offsetof(daObjKWheel01_c, mpModel)};
        out[1] = {offsetof(daObjKWheel01_c, mYAngularVelocity), offsetof(daObjKWheel01_c, m_klift_pid)};
        out[2] = {offsetof(daObjKWheel01_c, mKLiftBaseMatrices), offsetof(daObjKWheel01_c, mCreatedKLifts) + sizeof(daObjKWheel01_c::mCreatedKLifts)};
        return 3;
    case fpcNm_Obj_KLift00_e:
        out[0] = {offsetof(daObjKLift00_c, mNewBgMtx), offsetof(daObjKLift00_c, mpLiftPlatform)};
        out[1] = {offsetof(daObjKLift00_c, mNumChains), offsetof(daObjKLift00_c, mChainPositions)};
        out[2] = {offsetof(daObjKLift00_c, mNumChainModels), offsetof(daObjKLift00_c, mChainMdlObjs)};
        out[3] = {offsetof(daObjKLift00_c, mStopSwingingFrames), offsetof(daObjKLift00_c, mStopSwingingFrames) + sizeof(daObjKLift00_c::mStopSwingingFrames)};
        return 4;
    case fpcNm_Obj_Swpropeller_e:
        out[0] = {offsetof(daObjSwPr_c, mRotateSpeed), offsetof(daObjSwPr_c, mKaitenJntID)};
        out[1] = {offsetof(daObjSwPr_c, mSwDelayTimer), offsetof(daObjSwPr_c, mModelType) + sizeof(daObjSwPr_c::mModelType)};
        return 2;
    case fpcNm_Obj_Lv7PropY_e:
        out[0] = {offsetof(daPropY_c, mMode), offsetof(daPropY_c, field_0x5c4) + sizeof(daPropY_c::field_0x5c4)};
        return 1;
    case fpcNm_Obj_Prop_e:
        out[0] = {offsetof(daObjProp_c, mArg), offsetof(daObjProp_c, mArg) + sizeof(daObjProp_c::mArg)};
        return 1;
    case fpcNm_Obj_Fan_e:
        out[0] = {offsetof(daObjFan_c, field_0xacc), offsetof(daObjFan_c, field_0xad8) + sizeof(daObjFan_c::field_0xad8)};
        return 1;
    case fpcNm_Obj_Lv4Gear_e:
        out[0] = {offsetof(daObjLv4Gear_c, mTarget), offsetof(daObjLv4Gear_c, mSwActorID)};
        out[1] = {offsetof(daObjLv4Gear_c, mCount), offsetof(daObjLv4Gear_c, mType) + sizeof(daObjLv4Gear_c::mType)};
        return 2;
    case fpcNm_Obj_MagLift_e:
        out[0] = {offsetof(daMagLift_c, field_0x5ac), offsetof(daMagLift_c, is_switch) + sizeof(daMagLift_c::is_switch)};
        return 1;
    case fpcNm_Obj_MagLiftRot_e:
        out[0] = {offsetof(daMagLiftRot_c, mType), offsetof(daMagLiftRot_c, mShakeYAmp)};
        out[1] = {offsetof(daMagLiftRot_c, mShakeDecay), offsetof(daMagLiftRot_c, field_0x630) + sizeof(daMagLiftRot_c::field_0x630)};
        return 2;
    case fpcNm_Obj_MagneArm_e:
        out[0] = {offsetof(daObjMarm_c, mBgMtx1), offsetof(daObjMarm_c, mpBgW2)};
        out[1] = {offsetof(daObjMarm_c, mBgMtx3), offsetof(daObjMarm_c, mAcch)};
        out[2] = {offsetof(daObjMarm_c, mRotOffsetSwing), offsetof(daObjMarm_c, mID)};
        out[3] = {offsetof(daObjMarm_c, mPlayerRide), offsetof(daObjMarm_c, mpRope1)};
        out[4] = {offsetof(daObjMarm_c, field_0xA28), offsetof(daObjMarm_c, mShadowKey)};
        return 5;
    case fpcNm_Obj_RotBridge_e:
        out[0] = {offsetof(daRotBridge_c, mType), offsetof(daRotBridge_c, mBck)};
        out[1] = {offsetof(daRotBridge_c, mBckSpeed), offsetof(daRotBridge_c, mpBgW2)};
        out[2] = {offsetof(daRotBridge_c, mMtx), offsetof(daRotBridge_c, mMtx) + sizeof(daRotBridge_c::mMtx)};
        return 3;
    case fpcNm_Obj_Lv7Bridge_e:
        out[0] = {offsetof(daObjLv7Brg_c, field_0xa78), offsetof(daObjLv7Brg_c, mpBrokenModel)};
        out[1] = {offsetof(daObjLv7Brg_c, field_0xa94), offsetof(daObjLv7Brg_c, field_0xa98) + sizeof(daObjLv7Brg_c::field_0xa98)};
        return 2;
    case fpcNm_Obj_Lv4Bridge_e:
        out[0] = {offsetof(daObjLv4Brg_c, field_0x5bc), offsetof(daObjLv4Brg_c, field_0x5c4) + sizeof(daObjLv4Brg_c::field_0x5c4)};
        return 1;
    case fpcNm_Obj_Lv4Floor_e:
        out[0] = {offsetof(daObjLv4Floor_c, mMoveYPos), offsetof(daObjLv4Floor_c, mAction) + sizeof(daObjLv4Floor_c::mAction)};
        return 1;
    case fpcNm_Obj_Lv4RailWall_e:
        out[0] = {offsetof(daObjLv4Wall_c, mHeight), offsetof(daObjLv4Wall_c, mMode) + sizeof(daObjLv4Wall_c::mMode)};
        return 1;
    case fpcNm_Obj_Lv4PRwall_e:
        out[0] = {offsetof(daObjLv4PRwall_c, mBgMtx2), offsetof(daObjLv4PRwall_c, mpModel)};
        out[1] = {offsetof(daObjLv4PRwall_c, field_0x60c), offsetof(daObjLv4PRwall_c, field_0x629) + sizeof(daObjLv4PRwall_c::field_0x629)};
        return 2;
    case fpcNm_Obj_Lv4SlideWall_e:
        out[0] = {offsetof(daSldWall_c, mMode), offsetof(daSldWall_c, field_0x5b0) + sizeof(daSldWall_c::field_0x5b0)};
        return 1;
    case fpcNm_Obj_RotStair_e:
        out[0] = {offsetof(daObjRotStair_c, mWaterModelOn), offsetof(daObjRotStair_c, mWaterBgw)};
        out[1] = {offsetof(daObjRotStair_c, field_0x5c4), offsetof(daObjRotStair_c, mEventId)};
        out[2] = {offsetof(daObjRotStair_c, mAction), offsetof(daObjRotStair_c, field_0x5e3) + sizeof(daObjRotStair_c::field_0x5e3)};
        return 3;
    case fpcNm_Obj_MvStair_e:
        out[0] = {offsetof(daObjStair_c, mParticleLeftPos), offsetof(daObjStair_c, mSoundPos)};
        out[1] = {offsetof(daObjStair_c, mType), offsetof(daObjStair_c, mEventId)};
        out[2] = {offsetof(daObjStair_c, mMoveAccel), offsetof(daObjStair_c, mResType)};
        return 3;
    case fpcNm_Obj_SZbridge_e:
        out[0] = {offsetof(daObjSZbridge_c, mBgMtx), offsetof(daObjSZbridge_c, mpModel)};
        out[1] = {offsetof(daObjSZbridge_c, mWaterY), offsetof(daObjSZbridge_c, field_0x620) + sizeof(daObjSZbridge_c::field_0x620)};
        return 2;
    case fpcNm_Obj_Lv3Water_e:
        out[0] = {offsetof(daLv3Water_c, mMode), offsetof(daLv3Water_c, mEmitterIDs)};
        return 1;
    case fpcNm_Obj_Lv3Water2_e:
        out[0] = {offsetof(daLv3Water2_c, mMode), offsetof(daLv3Water2_c, mResIdx)};
        out[1] = {offsetof(daLv3Water2_c, mWaterLv), offsetof(daLv3Water2_c, mWestParticles) + sizeof(daLv3Water2_c::mWestParticles)};
        return 2;
    case fpcNm_Obj_WoodPendulum_e:
        out[0] = {offsetof(daObjWPndlm_c, field_0x958), offsetof(daObjWPndlm_c, field_0x95c) + sizeof(daObjWPndlm_c::field_0x95c)};
        return 1;
    case fpcNm_Obj_Lv8KekkaiTrap_e:
        out[0] = {offsetof(daKekaiTrap_c, mIsSwitch), offsetof(daKekaiTrap_c, mTimer) + sizeof(daKekaiTrap_c::mTimer)};
        return 1;
    case fpcNm_Obj_Lv8UdFloor_e:
        out[0] = {offsetof(daUdFloor_c, mMode), offsetof(daUdFloor_c, field_0x5c5) + sizeof(daUdFloor_c::field_0x5c5)};
        return 1;
    case fpcNm_Obj_SwSpinner_e:
        out[0] = {offsetof(daObjSwSpinner_c, mMtx), offsetof(daObjSwSpinner_c, mCount) + sizeof(daObjSwSpinner_c::mCount)};
        return 1;
    case fpcNm_Obj_Lv5FBoard_e:
        out[0] = {offsetof(daFlorBoad_c, mMode), offsetof(daFlorBoad_c, field_0x727) + sizeof(daFlorBoad_c::field_0x727)};
        return 1;
    case fpcNm_Obj_RopeBridge_e:
        out[0] = {offsetof(daObjRBridge_c, field_0xAEC), offsetof(daObjRBridge_c, mpLineMat)};
        out[1] = {offsetof(daObjRBridge_c, field_0xb08), offsetof(daObjRBridge_c, mSound)};
        return 2;
    case fpcNm_Obj_FallObj_e:
        out[0] = {offsetof(daObjFallObj_c, mMtx), offsetof(daObjFallObj_c, mpModel)};
        out[1] = {offsetof(daObjFallObj_c, mAction), offsetof(daObjFallObj_c, mSoundObj)};
        return 2;
    case fpcNm_Obj_Lv4Sand_e:
        out[0] = {offsetof(daObjLv4Sand_c, mHeight), offsetof(daObjLv4Sand_c, mMode) + sizeof(daObjLv4Sand_c::mMode)};
        return 1;
    case fpcNm_Obj_Lv4DigSand_e:
        out[0] = {offsetof(daObjL4DigSand_c, mGroundY), offsetof(daObjL4DigSand_c, mTimer) + sizeof(daObjL4DigSand_c::mTimer)};
        return 1;
    case fpcNm_Obj_StairBlock_e:
        out[0] = {offsetof(daStairBlock_c, mHit), offsetof(daStairBlock_c, mStts)};
        return 1;
