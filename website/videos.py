"""Public soundtrack and WIP showcase videos with click-to-load players."""
from __future__ import annotations

from html import escape
from music_credits import credit_text


YOUTUBE_CHANNEL = 'https://www.youtube.com/channel/UCQrVADTRa7sXr0GIeaq7SqQ'
YOUTUBE_PLAYLIST = 'https://www.youtube.com/playlist?list=PLPiXmw2nj9BM'
DISCORD = 'https://discord.gg/4YxQ7s69S'
VIDEO_IDS = {
    'menu-rock2': 'KWlri7ryuR0',
    'menu-alternative': 'YcaFpTO9C6Y',
    'town-rock': 't9dujaCADMg',
    'town-alternative': 'm59pZHxSQ_w',
    'town-third': 'TkMiffWTzPA',
}
VIDEO_TITLES = {
    'menu-rock2': 'Main Menu — Rock2',
    'menu-alternative': 'Main Menu',
    'town-rock': 'Tristram 1',
    'town-alternative': 'Tristram 2',
    'town-third': 'Tristram 3 — Alternate Export',
}
SHOWCASE_VIDEO_IDS = {
    'character-showcase': '_ByFLVksy9Q',
    'character-short': 'WgbIz3aJY9U',
}
SHOWCASE_VIDEO_TITLES = {
    'character-showcase': 'Griswold & Ogden | Fast 360° Character Showcase | Diablo3D',
    'character-short': 'Griswold + Ogden in 28 Seconds | Diablo3D #Shorts',
}


def _external_link(label, href, class_name=''):
    css = f' class="{escape(class_name, quote=True)}"' if class_name else ''
    return (
        f'<a{css} href="{escape(href, quote=True)}" target="_blank" '
        f'rel="noopener noreferrer">{escape(label)} <span aria-hidden="true">↗</span></a>'
    )


def _player_button(slot_id, video_id, player_title, label, title, *, icon=False, status_message=''):
    css = 'youtube-video-symbol youtube-video-play' if icon else 'youtube-video-load'
    content = '<span aria-hidden="true">▶</span>' if icon else escape(label)
    status = f' data-youtube-status-message="{escape(status_message, quote=True)}"' if status_message else ''
    return (
        f'<button type="button" class="{css}" hidden data-enhancement="youtube" '
        f'data-youtube-id="{video_id}" data-youtube-title="{escape(player_title, quote=True)}"{status} '
        f'aria-controls="{slot_id}" aria-expanded="false" '
        f'aria-label="{escape(label + ": " + title, quote=True)}">{content}</button>'
    )


def videos_section(language, url, featured=False):
    """Render PT/EN cards; ``url`` resolves the site's internal music route.

    This module has no publisher imports. The caller includes the local CSS and
    deferred JS; without JS, every video still has its ordinary YouTube link.
    """
    if language not in ('pt-BR', 'en'):
        raise ValueError('Video sections support pt-BR and en')
    english = language == 'en'

    def text(portuguese, translated):
        return translated if english else portuguese

    variant = 'featured' if featured else 'library'
    heading_id = f'youtube-{variant}-heading'
    heading = text('A trilha também está no YouTube', 'The soundtrack is on YouTube too')
    introduction = (
        text(
            'Comece por Main Menu — Rock2, uma regravação/reinterpretação da música de Diablo. Acompanhe os vídeos do projeto e participe da comunidade.',
            'Start with Main Menu — Rock2, a cover/reinterpretation of Diablo music. Follow the project’s videos and join the community.',
        ) if featured else text(
            'Cinco vídeos das regravações/reinterpretações da música de Diablo. Tristram 3 é uma exportação alternativa; cinco arquivos não representam cinco composições distintas.',
            'Five videos of covers/reinterpretations of Diablo music. Tristram 3 is an alternate export; five files do not represent five distinct compositions.',
        )
    )
    load_note = text(
        'Clique no play ou em “Ouvir aqui” para reproduzir neste card. O player do YouTube só carrega após esse clique.',
        'Click play or “Listen here” to play in this card. The YouTube player loads only after that click.',
    )
    library_text = text('Ouvir e baixar na biblioteca da trilha', 'Listen and download in the soundtrack library')
    keys = ['menu-rock2'] if featured else list(VIDEO_IDS)
    cards = []
    for key in keys:
        video_id = VIDEO_IDS[key]
        title = VIDEO_TITLES[key]
        slot_id = f'youtube-{variant}-{key}'
        play_label = text('Ouvir aqui', 'Listen here')
        player_title = text('Player do YouTube: ', 'YouTube player: ') + title
        version = text('Exportação alternativa', 'Alternate export') if key == 'town-third' else text('Regravação / reinterpretação', 'Cover / reinterpretation')
        cards.append(f'''<article class="youtube-video-card" data-youtube-card>
<div class="youtube-video-slot" id="{slot_id}" data-youtube-slot>
<div class="youtube-video-poster"><span class="youtube-video-mark" aria-hidden="true">D3D</span>{_player_button(slot_id, video_id, player_title, play_label, title, icon=True)}<span class="youtube-video-poster-title" aria-hidden="true">{escape(title)}</span></div>
</div>
<div class="youtube-video-copy"><p class="youtube-video-kind">{escape(version)}</p><h3>{escape(title)}</h3><p class="youtube-video-credit">{escape(credit_text(language))}</p>
<div class="youtube-video-actions">{_player_button(slot_id, video_id, player_title, play_label, title)}{_external_link(text('Ouvir no YouTube', 'Listen on YouTube'), 'https://www.youtube.com/watch?v=' + video_id)}</div>
<p class="sr-only" data-youtube-status role="status" aria-live="polite"></p></div>
</article>''')
    actions = ''.join((
        _external_link(text('Acompanhar o canal', 'Follow the channel'), YOUTUBE_CHANNEL, 'button'),
        _external_link(text('Participar no Discord', 'Join the Discord'), DISCORD, 'button secondary'),
        _external_link(text('Abrir a playlist', 'Open the playlist'), YOUTUBE_PLAYLIST, 'button secondary'),
    ))
    return f'''<section class="youtube-videos-section youtube-videos-{variant}" aria-labelledby="{heading_id}">
<div class="youtube-videos-intro"><p class="eyebrow">YOUTUBE · DIABLO 3D</p><h2 id="{heading_id}">{escape(heading)}</h2><p>{escape(introduction)}</p><p class="youtube-videos-note">{escape(load_note)} <a href="{escape(url('/musica/'), quote=True)}">{escape(library_text)}</a>.</p></div>
<div class="youtube-video-grid">{''.join(cards)}</div>
<div class="youtube-videos-community actions">{actions}</div>
</section>'''


def showcase_section(language, url):
    """Render the main horizontal WIP showcase and its secondary vertical Short.

    These two videos show the same models; they are separate from the five
    soundtrack videos and do not establish gameplay, integration or approval.
    """
    if language not in ('pt-BR', 'en'):
        raise ValueError('Showcase sections support pt-BR and en')
    english = language == 'en'

    def text(portuguese, translated):
        return translated if english else portuguese

    cards = []
    for key, video_id in SHOWCASE_VIDEO_IDS.items():
        short = key == 'character-short'
        title = SHOWCASE_VIDEO_TITLES[key]
        slot_id = f'youtube-showcase-{key}'
        play_label = text('Assistir aqui', 'Watch here')
        player_title = text('Player do YouTube: ', 'YouTube player: ') + title
        kind = text('Short vertical · 28 s · WIP', 'Vertical Short · 28 s · WIP') if short else text('Vídeo principal horizontal · 28 s · WIP', 'Main horizontal video · 28 s · WIP')
        explanation = text(
            'Os mesmos dois modelos, em formato vertical.',
            'The same two models, in a vertical format.',
        ) if short else text(
            'Griswold e Ogden em giro de 360° para revisão visual.',
            'Griswold and Ogden in a 360° turntable for visual review.',
        )
        status_message = text('Player aberto. Use os controles do vídeo para reprodução e volume.', 'Player opened. Use the video controls for playback and volume.')
        watch_url = 'https://www.youtube.com/shorts/' + video_id if short else 'https://www.youtube.com/watch?v=' + video_id
        css = 'youtube-showcase-short' if short else 'youtube-showcase-main'
        cards.append(f'''<article class="youtube-video-card {css}" data-youtube-card>
<div class="youtube-video-slot" id="{slot_id}" data-youtube-slot>
<div class="youtube-video-poster"><span class="youtube-video-mark" aria-hidden="true">D3D · WIP</span>{_player_button(slot_id, video_id, player_title, play_label, title, icon=True, status_message=status_message)}<span class="youtube-video-poster-title" aria-hidden="true">Griswold &amp; Ogden</span></div>
</div>
<div class="youtube-video-copy"><p class="youtube-video-kind">{escape(kind)}</p><h3>{escape(title)}</h3><p class="youtube-video-credit">{escape(explanation)}</p>
<div class="youtube-video-actions">{_player_button(slot_id, video_id, player_title, play_label, title, status_message=status_message)}{_external_link(text('Assistir no YouTube', 'Watch on YouTube'), watch_url)}</div>
<p class="sr-only" data-youtube-status role="status" aria-live="polite"></p></div>
</article>''')
    actions = ''.join((
        _external_link(text('Acompanhar o canal', 'Follow the channel'), YOUTUBE_CHANNEL, 'button'),
        _external_link(text('Participar no Discord', 'Join the Discord'), DISCORD, 'button secondary'),
    ))
    return f'''<section class="youtube-videos-section youtube-videos-showcase" aria-labelledby="youtube-showcase-heading">
<div class="youtube-videos-intro"><p class="eyebrow">{escape(text('MODELOS EM DESENVOLVIMENTO · WIP', 'MODELS IN DEVELOPMENT · WIP'))}</p><h2 id="youtube-showcase-heading">{escape(text('Griswold e Ogden em 360°', 'Griswold and Ogden in 360°'))}</h2><p>{escape(text('Uma prévia de 28 segundos dos dois modelos em desenvolvimento, com um vídeo principal horizontal e um Short dos mesmos personagens.', 'A 28-second preview of the two models in development, with a main horizontal video and a Short of the same characters.'))}</p><p>{escape(text('São estudos visuais em revisão. Os vídeos não mostram gameplay nem comprovam integração no jogo ou aprovação artística.', 'These are visual studies under review. The videos do not show gameplay or establish in-game integration or artistic approval.'))}</p><p class="youtube-videos-note">{escape(text('Clique no play ou em “Assistir aqui” para reproduzir neste card. O player só carrega após esse clique.', 'Click play or “Watch here” to play in this card. The player loads only after that click.'))} <a href="{escape(url('/devlog/'), quote=True)}">{escape(text('Acompanhar o devlog', 'Follow the devlog'))}</a>.</p></div>
<div class="youtube-video-grid">{''.join(cards)}</div>
<div class="youtube-videos-community actions">{actions}</div>
</section>'''
