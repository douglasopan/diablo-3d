# Igreja e iluminação de Tristram R11

Requisito humano de 12/10/2026 reabre escala/tonalidade da igreja R9/R10: jogador quase maior que a porta, pedra clara, ausência da luz vermelha; acrescenta modo dia/noite.

## Implementação e donos

`town_scene.cpp/AddCathedral` continua importando os bytes selecionados c7994e8d25c39569d5a260aa2291ffceb10882526503b073b3436f77bd672f1a. Transformação uniforme1.9x sobre escala12, resultado22.8, ancorada em (25,28.3970703125); altura22.0541. Todas4464 faces/UV/master preservadas. Cena reconstruída somente por seu lifecycle normal; revision church-user-r1-uniform22.8. SOL/gatilho/mapa/saves não mudam; footprint visual maior não amplia colisão nativa.

Adjunto de apresentação usa o renderer existente de aberturas e luzes: entrada recortada, pedra interna e duas regiões escuras frontais medidas nos triângulos/textura do master, com recessos inferidos e luz vermelha. Não é reconstrução completa do interior. Não se usam blockers GPU com aperturas: backend aceita somente blockers opacos; tentativa recusada está preservada no gate-r3. Iluminação vermelha da entrada/janelas é do adjunto; pintura vermelha nativa do terreno permanece, sem afirmar um sistema novo de point lights sobre todo o terreno.

`town_view.cpp` aplica graduação azul-cinza no material importado da igreja, sem regravar a textura original. `GraphicsOptions.townViewTimeOfDay` oferece Day0/Evening1/Night2, padrãoEvening. Aplicação no próximo frame e persistência pelo sistema normal de opções. Modos manuais; ciclo automático ainda não implementado. LUTs de mundo e horizonte/névoa mudam com a fase; UI, referência nativa e luz da Catedral não recebem filtro global. Albedo/meshes mantêm identidades: troca de fase renova LUT/revisão, não reconverte os masters a cada frame. Reset/dispose dos recursos existentes preservam a preferência gráfica.

## Evidência e entrega

EXE0486ef5c4228, versão privada `versions/diablo3d-preview-0486ef5c4228-town-lighting-r11`. Gate-final PASS125329: tamanho/âncora, recorte/vermelho, quatro ângulos GPU, mesmas vistas Dia/Entardecer/Noite, noite mais escura, CPU noturna, witness nativo/RNG, portas R10 e materiais/props/HUD, save/load emTown. Launcher VerifyOnly308. Assets307 iguais à R10, perfil privado sintético Speed32/1280x720/terceira pessoa/entardecer; sem alterar a R10 ou partida humana. Recibo diagnostics/town-lighting-20261012-r11/DELIVERY.json e Source congelada source-r11.

Falhas intermediárias preservadas: teste deixava GPU desativada após captura CPU (gate-r1, reparada fixture), tipo de cor da névoa na compilação (build-r3), blockers não suportados (gate-r3). Nenhum assert removido. Capturas finais conferidas; revisão humana/FPS sustentado/andares posteriores pendentes. Sem promoção habitual, web, Git ou Panvia.
