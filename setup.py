from setuptools import setup, Extension

sourcefiles = ["chub/chub.c", "chub/hubio.c", "chub/buffer.c"]

setup(
        name="chub",
        version="0.1",
        packages=["chub"],
        ext_modules=[Extension("chub.chub", sources=sourcefiles, 
        extra_compile_args=['-std=gnu99'], extra_link_args=['-lev'])],
        zip_safe=False, # This fixes some crazy circular import junk.
        url="https://github.com/jamwt/chub",
        description="Fast Hub for Diesel"
        )
