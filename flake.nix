{
  description = "emacs-mac 31.1";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05";
  };

  outputs =
    { nixpkgs, self }:

    let
      systems = nixpkgs.lib.platforms.darwin;
      forAllSystems =
        f:
        nixpkgs.lib.genAttrs systems (
          system:
          f {
            pkgs = import nixpkgs { inherit system; };
          }
        );
      make-emacs = import "${nixpkgs}/pkgs/applications/editors/emacs/make-emacs.nix";
      rev = self.rev or self.dirtyRev or "HEAD";
      emacs-mac = forAllSystems (
        { pkgs }:
        pkgs.callPackage
          (make-emacs {
            pname = "emacs-mac";
            version = "31.1.50";
            variant = "macport";
            src = self;
            meta = {
              homepage = "https://github.com/jdtsmith/emacs-mac";
              description = "Extensible, customizable GNU text editor - macport variant";
              longDescription = ''
                GNU Emacs is an extensible, customizable text editor—and more. At its core
                is an interpreter for Emacs Lisp, a dialect of the Lisp programming
                language with extensions to support text editing.

                The features of GNU Emacs include: content-sensitive editing modes,
                including syntax coloring, for a wide variety of file types including
                plain text, source code, and HTML; complete built-in documentation,
                including a tutorial for new users; full Unicode support for nearly all
                human languages and their scripts; highly customizable, using Emacs Lisp
                code or a graphical interface; a large number of extensions that add other
                functionality, including a project planner, mail and news reader, debugger
                interface, calendar, and more. Many of these extensions are distributed
                with GNU Emacs; others are available separately.

                This release initially was built from Mitsuharu Yamamoto's patched source code
                tailored for macOS. Moved to a fork of the latter starting with emacs v30 as the
                original project seems to be currently dormant.
              '';
              changelog = "https://github.com/jdtsmith/emacs-mac/blob/${rev}/NEWS-mac";
              license = pkgs.lib.licenses.gpl3Plus;
              platforms = systems;
              mainProgram = "emacs";
            };
          })
          {
            sigtool = pkgs.darwin.sigtool;
            srcRepo = true;
          }
      );
    in
    {
      packages = nixpkgs.lib.mapAttrs (system: drv: {
        emacs-mac = drv;
        default = drv;
      }) emacs-mac;

      overlays.default = final: prev: {
        emacs-mac = emacs-mac.${final.stdenv.system};
      };
    };
}
