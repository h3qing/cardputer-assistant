"""Link templates of url.<name> destinations (docs/25 §2.3): shapes and rendering, never string pasting."""

from __future__ import annotations

import pytest

from cpad.dest.template import TemplateError, check_template, render, scheme_of

T = "test-scheme://app/v1/prompt?text={text}&id={p}"


def test_a_good_template_and_its_focus_link():
    assert check_template(T, focus=False) == ("test-scheme", (("text", "{text}"), ("id", "{p}")))
    assert check_template("test-scheme://app/v1/agent?id={p}", focus=True)[0] == "test-scheme"
    assert check_template("test-scheme://open", focus=True) == ("test-scheme", ())
    assert check_template("test-scheme://h/new?q={text}&src=cardputer&project={project}", focus=False)
    assert scheme_of(T) == "test-scheme"


@pytest.mark.parametrize(
    ("template", "focus"),
    [
        (5, False), ("x" * 513, False), ("test-scheme://a?text={text}#x", False),
        ("test-scheme://a\\b?q={text}", False),
        ("Test://a?q={text}", False), ("t://a?q={text}", False), ("test-scheme:a?q={text}", False),
        ("test-scheme://{text}", False), ("test-scheme://a/{p}?q={text}", False), ("test-scheme://a b?q={text}", False),
        ("test-scheme://a?q", False), ("test-scheme://a?q={text}&api_key=1", False),
        ("test-scheme://a?q={text}&AuthToken={p}", False), ("test-scheme://a?q={text}&sig={p}", False),
        ("test-scheme://a?q={text}&v=has space", False), ("test-scheme://a?q={text}&v=abcdefghijklmnopq", False),
        ("test-scheme://a?q=pre{text}", False), ("test-scheme://a?q={p}", False),
        ("test-scheme://a?q={text}&r={text}", False), ("test-scheme://a?q={text}", True),
        ("test-scheme://a?=x&q={text}", False),
    ],
)  # fmt: skip
def test_bad_shapes_are_refused(template, focus):
    with pytest.raises(TemplateError):
        check_template(template, focus=focus)


def test_render_encodes_every_value_and_drops_empty_parameters():
    url = render(T, {"{text}": "[Cardputer] a+b & c=d/é 你好", "{p}": ""})
    assert url == "test-scheme://app/v1/prompt?text=%5BCardputer%5D%20a%2Bb%20%26%20c%3Dd%2F%C3%A9%20%E4%BD%A0%E5%A5%BD"
    assert render(T, {"{text}": "x", "{p}": "bot_1"}) == "test-scheme://app/v1/prompt?text=x&id=bot_1"


def test_render_keeps_literals_and_a_focus_link_without_values():
    assert (
        render("test-scheme://h/new?src=cardputer&q={text}", {"{text}": "x"}) == "test-scheme://h/new?src=cardputer&q=x"
    )
    assert render("test-scheme://app/v1/agent?id={p}", {}) == "test-scheme://app/v1/agent"
