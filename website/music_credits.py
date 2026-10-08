"""Editorial composition/recording credits, independent of the MP3 tag contract."""
from copy import deepcopy


COMPOSER = 'Matt Uelmen'
PRODUCER = 'Douglas Pan'
SOURCE = 'https://ftp.blizzard.com/pub/misc/Diablo.PDF'
CREDITS = {
    'pt-BR': 'Composição original: Matt Uelmen, para Diablo (Blizzard Entertainment). Regravação/reinterpretação produzida por Douglas Pan com auxílio de IA.',
    'en': 'Original composition: Matt Uelmen, for Diablo (Blizzard Entertainment). Cover/reinterpretation produced by Douglas Pan using AI.',
}


def credit_text(language):
    return CREDITS[language]


def composition(track):
    work = {
        '@type': 'MusicComposition',
        'composer': {'@type': 'Person', 'name': COMPOSER},
        'isPartOf': {'@type': 'VideoGame', 'name': 'Diablo',
                     'publisher': {'@type': 'Organization', 'name': 'Blizzard Entertainment'}},
        'citation': SOURCE,
    }
    if track['environment'] == 'Town':
        work['name'] = 'Tristram'
    # Main Menu is a local label; its official original title is unconfirmed.
    return work


def public_soundtrack(source, language):
    """Add public credits without rewriting source tags, hashes or audio bytes."""
    result = deepcopy(source)
    result['credits'] = {
        'text': credit_text(language),
        'original_composition': {'composer': COMPOSER, 'game': 'Diablo',
                                 'publisher': 'Blizzard Entertainment',
                                 'source_url': SOURCE, 'source_printed_page': 78},
        'recording_production': {'producer': PRODUCER, 'role': 'cover/reinterpretation',
                                 'ai_assistance': True},
    }
    for track in result['tracks']:
        track['credits'] = {'text': credit_text(language), 'recordingOf': composition(track),
                            'producer': PRODUCER, 'ai_assistance': True}
    result['background']['credits'] = deepcopy(result['credits'])
    return result


def album_metadata(music, language, canonical, absolute):
    producer = {'@type': 'Person', 'name': PRODUCER}
    credit = credit_text(language)
    return {
        '@context': 'https://schema.org', '@type': 'MusicAlbum',
        'name': music['album'], 'url': canonical, 'byArtist': producer,
        'creditText': credit, 'citation': SOURCE,
        'numTracks': len(music['tracks']), 'isAccessibleForFree': True,
        'track': [{
            '@type': 'MusicRecording', 'name': track['title'], 'byArtist': producer,
            'producer': producer, 'creditText': credit, 'recordingOf': composition(track),
            'duration': f"PT{round(track['duration_seconds'])}S",
            'audio': {'@type': 'AudioObject', 'contentUrl': absolute(track['public_path']),
                      'encodingFormat': 'audio/mpeg', 'creator': producer,
                      'creditText': credit, 'isBasedOn': composition(track)},
            'isAccessibleForFree': True,
        } for track in music['tracks']],
    }
