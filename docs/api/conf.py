# Sphinx configuration for the Paperboat API reference (docs/api).
# Build: npm run docs   (sphinx-build -W -b html docs/api docs/api/_build)
#
# The look follows the app's design system (apps/web/DESIGN.md): the theme in _themes/paperboat
# is a thin layer on Sphinx's `basic`; its colours and fonts are the app's tokens, generated
# into docs/_static/tokens.css by docs/gen_tokens.py (CI checks it is current), and the rules
# are in docs/_static/docs.css. docs/plan.html uses the same tokens.

project = "Paperboat"
author = "Paperboat"
release = "0"
version = "0 (protocol paperboat/0)"

extensions = []
exclude_patterns = ["_build"]
html_theme = "paperboat"
html_theme_path = ["_themes"]
html_static_path = ["../_static"]
html_css_files = ["tokens.css", "docs.css"]
html_title = "Paperboat API"
html_show_sphinx = False
highlight_language = "none"
primary_domain = None
cpp_index_common_prefix = ["paperboat::"]
