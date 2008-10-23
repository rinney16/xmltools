// XpathEvalDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Scintilla.h"
#include "XMLTools.h"
#include "XpathEvalDlg.h"
#include "Report.h"
#include <assert.h>
#include "LoadLibrary.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CXPathEvalDlg dialog

CXPathEvalDlg::CXPathEvalDlg(CWnd* pParent /*=NULL*/)
  : CDialog(CXPathEvalDlg::IDD, pParent)
{
  //{{AFX_DATA_INIT(CXPathEvalDlg)
  m_sExpression = _T("");
  //}}AFX_DATA_INIT
}


void CXPathEvalDlg::DoDataExchange(CDataExchange* pDX)
{
  CDialog::DoDataExchange(pDX);
  //{{AFX_DATA_MAP(CXPathEvalDlg)
  DDX_Text(pDX, IDC_EDIT_EXPRESSION, m_sExpression);
  //}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CXPathEvalDlg, CDialog)
  //{{AFX_MSG_MAP(CXPathEvalDlg)
  ON_BN_CLICKED(IDC_BTN_EVALUATE, OnBtnEvaluate)
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CXPathEvalDlg message handlers

HWND CXPathEvalDlg::getCurrentHScintilla(int which) {
  return (which == 0)?nppData._scintillaMainHandle:nppData._scintillaSecondHandle;
}

void CXPathEvalDlg::OnBtnEvaluate() {
  this->UpdateData();
  if (!m_sExpression.GetLength()) {
    Report::_printf_err(L"Empty expression; evaluation aborted.");
  } else {
    execute_xpath_expression(reinterpret_cast<const unsigned char*>((LPCTSTR)m_sExpression), NULL);
  }
}

/**
 * execute_xpath_expression:
 * @xpathExpr:    the xpath expression for evaluation.
 * @nsList:    the optional list of known namespaces in 
 *      "<prefix1>=<href1> <prefix2>=href2> ..." format.
 *
 * Parses input XML file, evaluates XPath expression and prints results.
 *
 * Returns 0 on success and a negative value otherwise.
 */
int CXPathEvalDlg::execute_xpath_expression(const xmlChar* xpathExpr, const xmlChar* nsList) {
  xmlDocPtr doc;
  xmlXPathContextPtr xpathCtx; 
  xmlXPathObjectPtr xpathObj; 

  assert(xpathExpr);

  int currentEdit, currentLength;
  ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&currentEdit);
  HWND hCurrentEditView = getCurrentHScintilla(currentEdit);

  currentLength = (int) ::SendMessage(hCurrentEditView, SCI_GETLENGTH, 0, 0);

  char *data = new char[currentLength+1];
  if (!data) return(-1);  // allocation error, abort check
  memset(data, '\0', currentLength+1);

  TextRange tr;
  tr.chrg.cpMin = 0;
  tr.chrg.cpMax = currentLength;
  tr.lpstrText = data;

  ::SendMessage(hCurrentEditView, SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));

  /* Load XML document */
  doc = pXmlReadMemory(data, currentLength, "noname.xml", NULL, 0);

  if (doc == NULL) {
    Report::_printf_err(L"Error: unable to parse XML.");
    delete [] data;
    return(-1);
  }

  /* Create xpath evaluation context */
  xpathCtx = pXmlXPathNewContext(doc);
  if(xpathCtx == NULL) {
    Report::_printf_err(L"Error: unable to create new XPath context\n");
    pXmlFreeDoc(doc); 
    delete [] data;
    return(-1);
  }
  
  /* Register namespaces from list (if any) */
  if((nsList != NULL) && (register_namespaces(xpathCtx, nsList) < 0)) {
    Report::_printf_err(L"Error: failed to register namespaces list \"%s\"\n", nsList);
    pXmlXPathFreeContext(xpathCtx); 
    pXmlFreeDoc(doc);
    delete [] data;
    return(-1);
  }

  /* Evaluate xpath expression */
  xpathObj = pXmlXPathEvalExpression(xpathExpr, xpathCtx);
  if(xpathObj == NULL) {
    Report::_printf_err(L"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr);
    pXmlXPathFreeContext(xpathCtx); 
    pXmlFreeDoc(doc);
    delete [] data;
    return(-1);
  }

  /* Print results */
  print_xpath_nodes(xpathObj);

  /* Cleanup */
  pXmlXPathFreeObject(xpathObj);
  pXmlXPathFreeContext(xpathCtx); 
  pXmlFreeDoc(doc);
  
  delete [] data;

  return(0);
}

/**
 * register_namespaces:
 * @xpathCtx:    the pointer to an XPath context.
 * @nsList:    the list of known namespaces in 
 *      "<prefix1>=<href1> <prefix2>=href2> ..." format.
 *
 * Registers namespaces from @nsList in @xpathCtx.
 *
 * Returns 0 on success and a negative value otherwise.
 */
int CXPathEvalDlg::register_namespaces(xmlXPathContextPtr xpathCtx, const xmlChar* nsList) {
  xmlChar* nsListDup;
  xmlChar* prefix;
  xmlChar* href;
  xmlChar* next;
  
  assert(xpathCtx);
  assert(nsList);

  nsListDup = pXmlStrdup(nsList);
  if(nsListDup == NULL) {
    Report::_printf_err(L"Error: unable to strdup namespaces list\n");
    return(-1);
  }
 
  next = nsListDup; 
  while(next != NULL) {
    /* skip spaces */
    while((*next) == ' ') next++;
    if((*next) == '\0') break;

    /* find prefix */
    prefix = next;
    next = (xmlChar*) pXmlStrchr(next, '=');
    if(next == NULL) {
      Report::_printf_err(L"Error: invalid namespaces list format\n");
      pXmlFree(nsListDup);
      return(-1);  
    }
    *(next++) = '\0';  
  
    /* find href */
    href = next;
    next = (xmlChar*) pXmlStrchr(next, ' ');
    if(next != NULL) {
      *(next++) = '\0';  
    }

    /* do register namespace */
    if (pXmlXPathRegisterNs(xpathCtx, prefix, href) != 0) {
      Report::_printf_err(L"Error: unable to register NS with prefix=\"%s\" and href=\"%s\"\n", prefix, href);
      pXmlFree(nsListDup);
      return(-1);  
    }
  }

  pXmlFree(nsListDup);
  return(0);
}

void AddToList(CListCtrl *list, CString type, CString name, CString value) {
  int idx = list->GetItemCount();
  list->InsertItem(idx, type);
  list->SetItemText(idx, 1, name);
  list->SetItemText(idx, 2, value);
}

/**
 * print_xpath_nodes:
 * @nodes:    the nodes set.
 * @output:    the output file handle.
 *
 * Prints the @nodes content to @output.
 */
void CXPathEvalDlg::print_xpath_nodes(xmlXPathObjectPtr xpathObj) {
  CString itemtype, itemname, itemvalue;
  CListCtrl *listresults = (CListCtrl*) this->GetDlgItem(IDC_LIST_XPATHRESULTS);
  
  listresults->DeleteAllItems();

  switch (xpathObj->type) {
    case XPATH_UNDEFINED: {
      Report::_printf_inf(L"Undefined expression.");
      break;
    }
    case XPATH_NODESET: {
      xmlNodeSetPtr	nodes = xpathObj->nodesetval;
      int size = (nodes) ? nodes->nodeNr : 0;

      if (size == 0) {
        itemtype = "";
        itemname = "No result";
        itemvalue = "";
        AddToList(listresults, itemtype, itemname, itemvalue);
      }

      for (int i = 0; i < size; ++i) {
        assert(nodes->nodeTab[i]);

        xmlNodePtr cur = nodes->nodeTab[i];

        if (cur->type == XML_ELEMENT_NODE) {
          itemtype = "Node";

          if (cur->ns) { 
            itemname = Report::cstring(L"%s:%s", cur->ns->href, cur->name);
            itemvalue = cur->content;
          } else {
            itemname = cur->name;
            // s'il y a du texte, on concat�ne tout le texte et on l'affiche
            if (cur->children) {
              xmlNodePtr txtnode = cur->children;
              itemvalue = "";
              while (txtnode != cur->last) {
			          if (txtnode->type == XML_TEXT_NODE) {
			            itemvalue += CString(txtnode->content);
			          }
                txtnode = txtnode->next;
              }
              if (txtnode->type == XML_TEXT_NODE)
                  itemvalue += CString(txtnode->content);
            }
            // si le noeud a des attributs, on les affiche (pour autant qu'on n'ait pas d�j� affich� les attributs)
            itemvalue.TrimLeft();
            itemvalue.TrimRight();
            if (itemvalue.IsEmpty() && cur->properties) {
              xmlAttrPtr attr = cur->properties;
              itemvalue = "";
              while (attr != NULL) {
                if (attr->type == XML_ATTRIBUTE_NODE)
                  itemvalue += Report::cstring(L"%s=\"%s\" ", attr->name, attr->children->content);
                attr = attr->next;
              }
            }
          }
        } else if (cur->type == XML_DOCUMENT_NODE) {
         
        } else if(cur->type == XML_ATTRIBUTE_NODE) {
          itemtype = "Attr";
          itemname = cur->name;

          if (cur->children) {
            itemvalue = cur->children->content;
          } else {
            itemvalue = "";
          }
        } else {
          Report::_printf_inf(L"%d",cur->type);
        /*
          XML_ELEMENT_NODE = 1
          XML_ATTRIBUTE_NODE = 2
          XML_TEXT_NODE = 3
          XML_CDATA_SECTION_NODE = 4
          XML_ENTITY_REF_NODE = 5
          XML_ENTITY_NODE = 6
          XML_PI_NODE = 7
          XML_COMMENT_NODE = 8
          XML_DOCUMENT_NODE = 9
          XML_DOCUMENT_TYPE_NODE = 10
          XML_DOCUMENT_FRAG_NODE = 11
          XML_NOTATION_NODE = 12
          XML_HTML_DOCUMENT_NODE = 13
          XML_DTD_NODE = 14
          XML_ELEMENT_DECL = 15
          XML_ATTRIBUTE_DECL = 16
          XML_ENTITY_DECL = 17
          XML_NAMESPACE_DECL = 18
          XML_XINCLUDE_START = 19
          XML_XINCLUDE_END = 20
          XML_DOCB_DOCUMENT_NODE = 21*/
      }


        AddToList(listresults, itemtype, itemname, itemvalue);
      }
      break;
    }
    case XPATH_BOOLEAN: {
      itemtype = "Bool";
      itemname = "";
      
      if (xpathObj->boolval) itemvalue = "true";
      else itemvalue = "false";

      AddToList(listresults, itemtype, itemname, itemvalue);
      break;
    }
    case XPATH_NUMBER: {
      itemtype = "Num";
      itemname = "";
      itemvalue = Report::cstring(L"%f", xpathObj->floatval);
      AddToList(listresults, itemtype, itemname, itemvalue);
      break;
    }
    case XPATH_STRING: {
      itemtype = "Str";
      itemname = "";
      itemvalue = CString(xpathObj->stringval);
      AddToList(listresults, itemtype, itemname, itemvalue);
      break;
    }
    /*case XPATH_POINT: Report::_printf_inf("XPATH_POINT"); break;
    case XPATH_RANGE: Report::_printf_inf("XPATH_RANGE"); break;
    case XPATH_LOCATIONSET: Report::_printf_inf("XPATH_LOCATIONSET"); break;
    case XPATH_USERS: Report::_printf_inf("XPATH_USERS"); break;
    case XPATH_XSLT_TREE: Report::_printf_inf("XPATH_XSLT_TREE"); break;*/
  }
}

BOOL CXPathEvalDlg::OnInitDialog() {
	CDialog::OnInitDialog();
	
  CListCtrl *listresults = (CListCtrl*) this->GetDlgItem(IDC_LIST_XPATHRESULTS);

	// Initialize the destination list control
	listresults->InsertColumn(0, L"Type", LVCFMT_LEFT, 50);
	listresults->InsertColumn(1, L"Name", LVCFMT_LEFT, 100);
	listresults->InsertColumn(2, L"Value", LVCFMT_LEFT, 400);

  listresults->DeleteAllItems();

  CRect myRect;
  GetClientRect(&myRect);
  ClientToScreen(myRect);
  MoveWindow(myRect.left+100, myRect.top+100, myRect.Width(), myRect.Height());

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CXPathEvalDlg::OnSize(UINT nType, int cx, int cy) {
	CDialog::OnSize(nType, cx, cy);
	
  CWnd *btn_wnd = GetDlgItem(IDC_BTN_EVALUATE);
  CWnd *in_wnd = GetDlgItem(IDC_EDIT_EXPRESSION);
  CWnd *out_wnd = GetDlgItem(IDC_LIST_XPATHRESULTS);

  if (btn_wnd && in_wnd && out_wnd) {
    const int border = 8;
    const int wndspace = 6;
    const int btnwidth = 80;
    const int btnheight = 28;
    const int inheight = 100;

    btn_wnd->MoveWindow(cx-border-btnwidth,
                        border,
                        btnwidth,
                        btnheight);
	  in_wnd->MoveWindow(border,
                       border,
                       cx-2*border-btnwidth-wndspace,
                       inheight);
    out_wnd->MoveWindow(border,
                        border+inheight+wndspace,
                        cx-2*border,
                        cy-2*border-inheight-wndspace);
  }
}
