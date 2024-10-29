int i = 0;

void bhv_nup_interact(void){

    if (obj_check_if_collided_with_object(o, gMarioObject)) {
        play_sound(SOUND_GENERAL_COLLECT_1UP, gGlobalSoundSource);
        i++;
        gMarioState->healCounter += i;
        gMarioState->numLives += o->oBehParams2ndByte;
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        gMarioState->numCoins += i;
        gMarioState->squishTimer += i;
        i++;
    }
}

void bhv_nup_loop(void) {
    bhv_nup_interact();
}
