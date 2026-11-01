import pytest

from gate_mer import mer, tokens


def test_tokens_split_cjk_by_character_and_latin_by_word():
    assert tokens("今天开 Sync Meeting, v0.3 的 pipeline！") == ["今", "天", "开", "sync", "meeting", "v0", "3", "的", "pipeline"]


def test_tokens_ignore_punctuation_and_case():
    assert tokens("“Hello”，世界。") == ["hello", "世", "界"]
    assert tokens("") == []


def test_mer_counts_substitutions_insertions_deletions():
    assert mer("我们 开会", "我们 开会") == 0.0
    assert mer("我们 开会", "我门 开会") == pytest.approx(25.0)  # 1 of 4 tokens substituted
    assert mer("hello world", "hello") == pytest.approx(50.0)  # deletion
    assert mer("hello", "hello big world") == pytest.approx(200.0)  # insertions can exceed 100


def test_mer_needs_a_reference():
    with pytest.raises(ValueError):
        mer("，。", "anything")
