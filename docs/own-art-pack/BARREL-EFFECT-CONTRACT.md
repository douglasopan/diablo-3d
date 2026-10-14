# Barril humano — modelo e efeito separados

12/10/2026. Fonte humana preservada: wooden+barrel+3d+model.glb. Sete partes rígidas,938 triângulos,UV e resolução de texturas preservados; escala uniforme1,footprint nativo1célula.

OBJ_BARREL e OBJ_BARRELEX usam mesh enquanto !Object::IsBroken(). Ao quebrar (_oBreak=-1), DrawBarrelNativeBreakEffect usa object.currentSprite() e _oAnimFrame nativos, incluindo explosão e restos terminais. Não é animação de fragmentação de malha nova. Essa função é a fronteira para trocar o efeito visual posteriormente; não deve tocar BreakBarrel, dano, chain reactions, sons, RNG, loot, colisão, saves ou rede. Novos efeitos precisam identificar normal/explosivo, frames/pivô, terminal e descarte/reset antes da promoção.

D3DMESH1 conserva textura RGB master,render da Catedral mapeia em paleta/luz nativa. Não declarar PBR completo. Hashes fixados no loader; caches invalidam no reset e atualização de paleta. Handoff/recibo privados: diagnostics/barrel-integration-20261012-r1. PASS111.594,normal/explosivo estados1,2,terminalGPU e witness restaurado. Revisão humana ainda pendente. SemDamnationX.

## R5 — defeito humano de restos antigos

Substitui quebra normal/terminal por fragmentos da mesh original, agrupados em12setores angulares por parte. Separação/rotação/arco/assentamento derivados do frame nativo e identidade da célula, sem consumir RNG. Final permanece estático no chão; save/load reconstrói pelo estado nativo. Sprite nativo somente frames ativos explosivos ou fallback sem modelo. Cadáveres planoXZ fixo/direção/TRN nativos. ResetTownViewResources descarta modelos/cache/counters. PASS111.610 e VerifyOnly238; revisão humana pendente.
