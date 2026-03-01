// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "BlasterPlayerState.generated.h"

/**
 * 
 */
UCLASS()
class BLASTER_API ABlasterPlayerState : public APlayerState
{
	GENERATED_BODY()
	
public:
	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;
	
	/*
	* Replication notifies
	*/
	virtual void OnRep_Score() override;

	UFUNCTION()
	virtual void OnRep_Defeats();
	void AddToScore(float ScoreAmount);
	void AddToDefeats(int32 DefeatsAmount);

private:
	// Making sure these variables are set as nullptr so that they are not uninitialized
	// If they are uninitialized, then they are technically not a "nullptr" and could pass
	// "if-checks" later in the code which would cause major issues. You can initialize them
	// to nullptrs by giving them a UPROPERTY or explicitly setting them equal to nullptr here
	UPROPERTY()
	class ABlasterCharacter* Character;
	UPROPERTY()
	class ABlasterPlayerController* Controller;
	
	UPROPERTY(ReplicatedUsing = OnRep_Defeats)
	int32 Defeats;
};
