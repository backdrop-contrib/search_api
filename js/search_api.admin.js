/**
 * @file
 * Javascript enhancements for the Search API admin pages.
 */

(function ($) {

/**
 * Allows the re-ordering of enabled data alterations and processors.
 */
// Copied from filter.admin.js
Backdrop.behaviors.searchApiStatus = {
  attach: function (context, settings) {
    $('.search-api-status-wrapper input.form-checkbox', context).once('search-api-status', function () {
      var $checkbox = $(this);
      // Retrieve the tabledrag row belonging to this processor.
      var $row = $('#' + $checkbox.attr('id').replace(/-status$/, '-weight'), context).closest('tr');
      // Retrieve the vertical tab belonging to this processor.
      var $tab = $('#' + $checkbox.attr('id').replace(/-status$/, '-settings'), context).data('verticalTab');

      // Bind click handler to this checkbox to conditionally show and hide the
      // filter's tableDrag row and vertical tab pane.
      $checkbox.bind('click.searchApiUpdate', function () {
        if ($checkbox.is(':checked')) {
          $row.show();
          if ($tab) {
            $tab.tabShow().updateSummary();
          }
        }
        else {
          $row.hide();
          if ($tab) {
            $tab.tabHide().updateSummary();
          }
        }
        // Restripe table after toggling visibility of table row.
        Backdrop.tableDrag['search-api-' + $checkbox.attr('id').replace(/^edit-([^-]+)-.*$/, '$1') + '-order-table'].restripeTable();
      });

      // Attach summary for configurable items (only for screen-readers).
      if ($tab) {
        $tab.fieldset.backdropSetSummary(function (tabContext) {
          return $checkbox.is(':checked') ? Backdrop.t('Enabled') : Backdrop.t('Disabled');
        });
      }

      // Trigger our bound click handler to update elements to initial state.
      $checkbox.triggerHandler('click.searchApiUpdate');
    });
  }
};

})(jQuery);
