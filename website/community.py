"""Optional community invitation shared by Portuguese and English pages."""

from html import escape


DISCORD_URL = "https://discord.gg/4YxQ7s69S"
YOUTUBE_URL = "https://www.youtube.com/channel/UCQrVADTRa7sXr0GIeaq7SqQ"

_COPY = {
    "pt": {
        "lang": "pt-BR",
        "eyebrow": "Faça parte do Diablo 3D",
        "title": "Ajude esta comunidade a crescer",
        "text": "Ajude o Diablo 3D a crescer: entre no Discord para conversar com a comunidade e acompanhe as novidades do projeto no YouTube.",
        "discord": "Entrar no Discord",
        "youtube": "Acompanhar no YouTube",
        "voluntary": "A participação é voluntária. Você pode abrir este convite pelo rodapé quando quiser.",
        "dismiss": "Agora não",
        "close": "Fechar convite",
    },
    "en": {
        "lang": "en",
        "eyebrow": "Be part of Diablo 3D",
        "title": "Help this community grow",
        "text": "Help Diablo 3D grow: join Discord to talk with the community and follow the project's updates on YouTube.",
        "discord": "Join Discord",
        "youtube": "Follow on YouTube",
        "voluntary": "Taking part is optional. You can open this invitation from the footer whenever you like.",
        "dismiss": "Not now",
        "close": "Close invitation",
    },
}


def community_invite(language: str) -> str:
    """Return one native dialog; the caller supplies CSS, deferred JS and launcher.

    Launchers are buttons with ``data-community-open``, ``aria-haspopup="dialog"``
    and ``aria-controls="community-invite"``. Both languages share storage keys.
    """
    key = "en" if (language or "").lower().startswith("en") else "pt"
    copy = {name: escape(value, quote=True) for name, value in _COPY[key].items()}
    return f'''<dialog id="community-invite" class="community-invite" lang="{copy['lang']}" aria-labelledby="community-invite-title" aria-describedby="community-invite-text">
  <button type="button" class="community-invite-close" data-community-close aria-label="{copy['close']}" autofocus><span aria-hidden="true">×</span></button>
  <p class="community-invite-eyebrow">{copy['eyebrow']}</p>
  <h2 id="community-invite-title">{copy['title']}</h2>
  <p id="community-invite-text">{copy['text']}</p>
  <div class="community-invite-actions">
    <a class="community-invite-link" href="{DISCORD_URL}" target="_blank" rel="noopener noreferrer" data-community-visit>{copy['discord']}</a>
    <a class="community-invite-link community-invite-link-secondary" href="{YOUTUBE_URL}" target="_blank" rel="noopener noreferrer" data-community-visit>{copy['youtube']}</a>
  </div>
  <p class="community-invite-note">{copy['voluntary']}</p>
  <button type="button" class="community-invite-later" data-community-close>{copy['dismiss']}</button>
</dialog>'''
