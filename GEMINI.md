# UE5 GAS Project Development Rules & Invariants

## 1. Architectural Integrity & Reuse
- **Faction & Hostility Checks**: Always reuse `CFLF_isHostileTarget` and `CFLF_getActorFactionName` from `FPSAnimFunctionLibrary`. Never introduce redundant or conflicting faction checking functions.
- **Damage Pipeline**: All damage application must flow through `CFLF_ApplyDamageToTarget` using `GE_Damage` with `Data.Damage` SetByCaller magnitude, ensuring consistent combat feedback and death event triggers.
- **Death Handling**: Death must be dispatched via `ADoomBaseCharacter::OnCharacterDeath` directly from `PostGameplayEffectExecute` in `DoomAttributeSet`.

## 2. Blueprint Function Library (C++) Invariants
- **Parameter Defaults & Fallbacks**: Avoid unwired `const FVector&` parameters. Always use value types with default values (`FVector CenterLocation = FVector::ZeroVector`) and include safe fallback logic in C++ (e.g., falling back to `ContextActor->GetActorLocation()` if nearly zero).
- **GAS Const-Correctness**: When routing GAS event instigators (`const AActor*`) to BlueprintImplementableEvents (`AActor*`), perform a safe `const_cast<AActor*>` with null checks.

## 3. Actor Component & Collision Hierarchies
- **Projectile Blueprints**: In any projectile Blueprint using `ProjectileMovementComponent`, the physical collision primitive (e.g. `USphereComponent`) **MUST be the RootComponent** (replace `DefaultSceneRoot`). Ensure `bSimulationGeneratesHitEvents = true` and collision channels are set to Block intended targets (`Pawn`, `WorldStatic`, etc.).
- **Pickup Overlap Safety**: Any pickup actor (`BPA_pickupBase` derivatives) MUST gate overlap events with `Is Player Controlled` (or explicit faction filtering) before applying GameplayEffects to prevent dying enemies from consuming their own drops.

## 4. Weapon & Projectile Design Patterns
- **Tarkov-Style vs Weapon-Driven Damage**: Projectile properties (e.g., `rawDamage`, projectile velocity) should be marked `BlueprintReadWrite, EditAnywhere, meta = (ExposeOnSpawn = true)`. This allows Gameplay Abilities to either override damage at spawn time or leave it disconnected to fall back to the ammunition's native profile.
