# The Python pycgfx asks for (its Pipfile: python 3.12, gltflib, pillow), at the versions of the image that made
# app/meta/banner.cgfx (python:3.12-slim on 2026-11-16, then 3.12.16; its pip freeze below).
# Run with --network none and the pycgfx checkout mounted read-only (build.py does).
#   docker build -t banner-pycgfx:1 tools/banner/container
FROM python:3.12.16-slim
RUN pip install --no-cache-dir gltflib==1.0.13 pillow==13.0.0 dataclasses-json==0.6.7 marshmallow==3.26.2 \
        mypy_extensions==1.2.0 packaging==26.3 typing-inspect==0.9.0 typing_extensions==4.16.0 \
    && pip freeze > /pip-freeze.txt
