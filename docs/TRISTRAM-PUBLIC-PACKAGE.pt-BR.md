# Distribuicao completa da Tristram selecionada

A Tristram da local3 ficou fora da FULL3 porque o snapshot continha imagens originais do Diablo embutidas no arquivo estatico e na paleta. O launcher anterior exigia MPQ para o jogo, mas apenas copiava a cena privada: nao recuperava esses materiais a partir do arquivo do jogador. Isso nao atendia ao pedido de distribuir o checkpoint completo.

O novo pacote inclui o componente interno de preparacao, os 14 prefabs arquitetonicos selecionados, os tres recursos proprios do Warrior, os assets R17, o mesmo executavel local3, bibliotecas, defaults, licencas e instrucoes PT/EN. As imagens estaticas e a paleta originais nao sao distribuidas: na primeira abertura, sao criadas localmente a partir do DIABDAT.MPQ original.

A base gerada deve corresponder ao SHA256 `d463e0dee0f022f7b5ecbf9912a787215fa8608fcab7077fbf8cf2fc591198ff`. Aplicamos os 20 bytes de edicoes selecionadas, verificando os valores anteriores. O arquivo final deve corresponder a `01fdec4c0c2279ed1f467056731fb1c056633a7e10d595279632c9f7102c53df`. Todos os 19 arquivos da cena sao conferidos por tamanho/hash antes de instalar. Nao usamos um fallback como substituto da cena selecionada.

O loader nativo real admite 14 arquiteturas, 25.743 objetos fixos e 1.349 texturas, com fallback arquitetonico desabilitado. O WorldPatch original aplica 13 celulas, quatro objetos e quatro rotas. Manifesto, geometria, transformacoes, UVs, materiais, identidades e patch logico correspondem ao snapshot cardinal R2. O executavel continua `0f4d9290c0589cbee3eb6c22791f373d4a1b67dd618b97e3bfb5a86637f9bd6c`, sem otimizacoes novas.

Os tres RGBs arquitetonicos sao identicos aos recursos proprios de cabana, igreja e casa de Pepin ja publicados na R17. A prova esta em `assets/own-tristram-local3/texture-provenance.json`; os demais prefabs nao contem RGB embutido. O arquivo estatico nativo D3DSTAT1 e a paleta original nao entram no Git nem na release.

A preparacao nao abre janela, dispositivo de video/audio, input fisico, gameplay ou saves. Preserva MPQ, mapa humano e editor. Reuso de perfil conserva configuracoes e dados persistentes; cena modificada ou MPQ diferente sao recusados, sem substituicao automatica. Dados originais incompativeis nao passam na verificacao da base congelada.

Abra Iniciar-Diablo3D.cmd, selecione DIABDAT.MPQ e aguarde a primeira preparacao. Nao precisa de Python, compilador, conversor externo ou download privado. Perfil/saves separados: `%LOCALAPPDATA%/Diablo3D-local3-Tristram/profile`. K alterna primeira/terceira pessoa; Home restaura terceira; F4 alterna 3D/nativo. Warrior Light com Short Sword/Buckler: Tristram e Catedral 1. Desempenho permanece pendente; esses testes de instalacao/loader nao equivalem a novo gameplay fisico ou FPS sustentado.

[English instructions](TRISTRAM-PUBLIC-PACKAGE.en.md)
