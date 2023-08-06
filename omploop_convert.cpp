// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/LiveVariables.h"

#include <stdio.h>
#include <unistd.h>
#include <set>
#include <regex>

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;


class MyVisitor : public RecursiveASTVisitor<MyVisitor> {
private:
  ASTContext *astContext; // used for getting additional AST info
  Rewriter *rewriter;
  void get_used_vars(const Stmt *curr, std::set<const VarDecl *> &used)
  {
    if (!curr) {
      return;
    } else {
      printf("+++ %s\n", curr->getStmtClassName());
    }
    if (auto dstmt = dyn_cast<DeclStmt>(curr)) {
      for (auto decl: dstmt->decls()) {
	if (const VarDecl *vd = dyn_cast<VarDecl>(decl)) {
	  used.insert(vd);
	}
      }
    }
    if (auto fstmt = dyn_cast<ForStmt>(curr)) {
      auto inc = fstmt->getInc();
      for (auto inc_op : inc->children()) {
	if (auto dr_exp = dyn_cast<DeclRefExpr>(inc_op)) {
	  auto decl = dr_exp->getDecl();
	  if (const VarDecl *vd = dyn_cast<VarDecl>(decl)) {
	    used.insert(vd);
	  }
	}
      }
    }
    for (auto child : curr->children()) {
      get_used_vars(child, used);
    }
  }
  
public:
  explicit MyVisitor(CompilerInstance *CI, Rewriter *R) 
      : astContext(&(CI->getASTContext())) // initialize private members
    {
      rewriter = R;
    }
    virtual bool VisitStmt(Stmt *st) {
      //st->dump();
      return true;
    }

  
    virtual bool VisitOMPLoopDirective(OMPLoopDirective *omp) {
      for (const Expr *u: omp->updates()) {
	if (!u) {
	  printf("Skipped this directive. I don't know why some OMPLoopDirectives don't work.\n");
	  return true;
	}
      }
      AnalysisDeclContextManager adcm(*astContext);
      auto top_decl = omp->getInnermostCapturedStmt()->getCapturedDecl()->getNonClosureContext();
      AnalysisDeclContext *adc = adcm.getContext(top_decl);
      adc->getCFGBuildOptions().setAllAlwaysAdd();
      CFG& cfg = *adc->getCFG();
      LiveVariables *lv = adc->getAnalysis<LiveVariables>();
      std::set<const VarDecl *> used;
      omp->getInnermostCapturedStmt()->dump();
      get_used_vars(omp->getInnermostCapturedStmt()->getCapturedDecl()->getBody(), used);
      //cfg.print(llvm::outs(), astContext->getLangOpts(), false);
      for (auto vd: used) {
	//std::cerr << "used_var : " << name << "\n";
	printf("used_var : %s\n", vd->getNameAsString().c_str());
      }
      //lv->dumpBlockLiveness(astContext->getSourceManager());

      std::string str_push = "";
      std::string str_pop = "";
#if 1
      std::string new_array;
      for (auto B: cfg) {
	if (B->getTerminator().isValid()) {
	  CFGTerminator T = B->getTerminator();
	  if (T.getKind() == CFGTerminator::StmtBranch) {
	    auto Stmt = T.getStmt();
	    if (const GotoStmt *GTStmt = dyn_cast<GotoStmt>(Stmt)) {
	      LabelDecl *ldecl = GTStmt->getLabel();
	      std::regex re("my_yield");
	      bool is_my_yield = std::regex_search(ldecl->getName().str(),re);
	      if (is_my_yield) {
		for (auto vd: used) {
		  bool islive = lv->isLive(B, vd);
		  QualType qtype = vd->getTypeSourceInfo()->getType();
		  printf("live used_var : %s %d %s\n", vd->getNameAsString().c_str(), islive,
			 qtype.getAsString().c_str());
		  if (islive) {
		    new_array += qtype.getAsString() + " __" + vd->getNameAsString() + "[N_CTX];\n";
		    str_push += "__" + vd->getNameAsString() + "[i_ctx] = " + vd->getNameAsString() + ";\n";
		    str_pop += vd->getNameAsString() + " = __" + vd->getNameAsString() + "[i_ctx];\n";
		  }
		}
	      }
	    }
	  }
	}
      }
#endif
      omp->dumpPretty(*astContext);
      //omp->dump();
      //omp->getLastIteration()->dump();
      
      //omp->getIterationVariable()->dump();
      //omp->getIterationVariable()->dump();
      
      std::string str_init;
      llvm::raw_string_ostream os_init(str_init);
      for (const Expr *ini: omp->inits()) {
	//ini->dump();
	ini->printPretty(os_init, nullptr, PrintingPolicy(astContext->getLangOpts()));
      }

      std::string str_cond;
      llvm::raw_string_ostream os_cond(str_cond);
      omp->getCond()->printPretty(os_cond, nullptr, PrintingPolicy(astContext->getLangOpts()));
      {
	std::size_t pos1 = str_cond.find(".omp.iv");
	str_cond.replace(pos1, 7, "my_th_iter");
	std::size_t pos2 = str_cond.find(".omp.ub");
	str_cond.replace(pos2, 7, "my_th_iter_max");
      }
      
      std::string str_update;
      llvm::raw_string_ostream os_update(str_update);
      for (const Expr *u: omp->updates()) {
	//u->dump();
	u->printPretty(os_update, nullptr, PrintingPolicy(astContext->getLangOpts()));
      }
      std::size_t pos = str_update.find(".omp.iv");
      str_update.replace(pos, 7, "my_th_iter");
      
      std::string str_head;
      llvm::raw_string_ostream os_head(str_head);

      std::string str_body;
      llvm::raw_string_ostream os_body(str_body);
      omp->getBody()->printPretty(os_body, nullptr, PrintingPolicy(astContext->getLangOpts()));

      os_head << "\n"
	      << new_array <<
	"  int my_th_iter = 0;\n"
	"  int my_th_iter_max = ";
      omp->getLastIteration()->printPretty(os_head, nullptr, PrintingPolicy(astContext->getLangOpts()));

      os_head << ";\n"
	"  int n_done = 0;\n"
	"  int stat[N_CTX];\n"
	"  bzero(stat, N_CTX*sizeof(int));\n"
	"  do {\n"
	"    for (int i_ctx=0; i_ctx<N_CTX; i_ctx++) {\n"
	"      switch (stat[i_ctx]) {\n"
	"        case -1: goto __exit;\n"
	"        case 0:\n"
	"          while (1) {\n"
	"            if (!(" << str_cond << ")) {\n"
	"              stat[i_ctx] = -1;\n"
	"              n_done ++;\n"
	"              goto __exit;\n"
	"            } // if\n"
	"            " << str_update << ";\n"
	"            my_th_iter++;\n"
	      << str_push <<
	"            stat[i_ctx] = __LINE__; goto __exit; case __LINE__: \n"
	      << str_pop << str_body <<
	"          } // while (1)\n"
	"      } // switch (stat[i_ctx])\n"
	"    __exit:\n"
	"    } // for i_ctx\n"
	"  } while (n_done < N_CTX);\n";
      rewriter->RemoveText(omp->getSourceRange());
      rewriter->ReplaceText(omp->getInnermostCapturedStmt()->getSourceRange(), str_head);
      return true;
    }
};

class MyASTConsumer : public ASTConsumer {
private:
  MyVisitor *visitor;
public:
  MyASTConsumer(CompilerInstance *CI, Rewriter *R) {
    visitor = new MyVisitor(CI, R);
  }
  virtual void HandleTranslationUnit(ASTContext &Context) override {
    visitor->TraverseDecl(Context.getTranslationUnitDecl());
  }
};

class MyAction : public ASTFrontendAction {
public:
  // Output the edit buffer for this translation unit
  void EndSourceFileAction() override {
    MyRewriter.getEditBuffer(MyRewriter.getSourceMgr().getMainFileID())
      .write(llvm::outs());
  }
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
						 StringRef file) override {
    MyRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<MyASTConsumer>(&CI, &MyRewriter);
  }
private:
  Rewriter MyRewriter;
};
  
// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser& OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<MyAction>().get());
}
